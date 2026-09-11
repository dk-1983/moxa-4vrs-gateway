/* Fixed storage, single-threaded HTTP/1.1 scheduler. No request pipelining.
 * One bounded TLS/read/write step per client per sweep; idle peers never wait
 * inside a client handler. The shared IPC stream has exactly one owner. */
#define WEB_CLIENTS 1U
#include <netinet/tcp.h>
#define WEB_REQUESTS 32U
#include "web/web_budget.h"
static web_budget_t client_budget;
enum {CLIENT_TLS=1,CLIENT_READ,CLIENT_QUEUE,CLIENT_IPC,CLIENT_WRITE,CLIENT_DRAIN};
typedef struct web_client {
 int fd,secure,state,want,close,head;
 unsigned int requests;
 uint32_t since;
 mbedtls_ssl_context ssl;
 web_http_t request;
 char authority[40],destination[40],input[WEB_HTTP_MAX+1];
 char body[WEB_BODY_MAX+1],json[WEB_BODY_MAX+256],header[1024];
 const unsigned char *output;const web_asset_t *asset;
 size_t used,header_size,header_sent,output_size,output_sent;
} web_client_t;
static web_client_t clients[WEB_CLIENTS];
static int ipc_owner=-1;
static web_frame_t ipc_tx,ipc_rx;
static uint32_t ipc_since;
static unsigned int round_robin;
static int clients_result;
#ifdef WEB_HOST_TEST
static uint32_t budget_logged_peak;
#endif
static void client_free(web_client_t *c){
 if(c->fd>=0){close(c->fd);mbedtls_ssl_free(&c->ssl);}
 web_clear(c,sizeof(*c));c->fd=-1;
}
static void client_response(web_client_t*c,int code,const char*type,const void*data,size_t n,const char*extra,int cache){
 int size;c->close|=c->request.close||c->requests>=WEB_REQUESTS;
 size=snprintf(c->header,sizeof(c->header),
  "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: %s\r\nCache-Control: %s\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: %s\r\nReferrer-Policy: no-referrer\r\n%s\r\n",
  code,code==304?"Not Modified":code==503?"Busy":code<400?"OK":"Request failed",type,(unsigned long)n,c->close?"close":"keep-alive",cache?"public, no-cache":"no-store",web_inline_csp,extra?extra:"");
 if(size<0||(size_t)size>=sizeof(c->header)){client_free(c);return;}
 c->header_size=(size_t)size;c->header_sent=c->output_sent=0;
 c->output=data;c->output_size=c->head||code==304?0:n;
 c->state=CLIENT_WRITE;c->since=web_now();c->want=1;
}
static void client_error(web_client_t*c,int code){
 c->close=1;client_response(c,code,"application/json","{\"error\":\"Request failed\"}",26,code==503?"Retry-After: 2\r\n":NULL,0);
}
static void client_asset(web_client_t*c){
 char extra[100],tag[70];int public_asset=strncmp(c->asset->path,"/help/",6)!=0,match=web_http_etag_match(c->request.etag,c->asset->etag);
 if(public_asset&&match<0){client_error(c,400);return;}
 snprintf(tag,sizeof(tag),"\"%s\"",c->asset->etag);
 snprintf(extra,sizeof(extra),"ETag: %s\r\n",tag);
 /* Help remains authenticated and no-store, even though its bytes are shared. */
 client_response(c,public_asset&&match?304:200,c->asset->type,c->asset->bytes,c->asset->size,public_asset?extra:NULL,public_asset);
}
static void client_route(web_client_t*c){
 int r;const web_asset_t*a;
 c->head=!strcmp(c->request.method,"HEAD");++c->requests;
 if(!c->request.write)for(a=web_assets;a->path;++a)if(!strcmp(a->path,c->request.path)){
  c->asset=a;if(strncmp(a->path,"/help/",6)){client_asset(c);return;}
  snprintf(c->body,sizeof(c->body),"op=help\nsession=%s\n",c->request.session);c->state=CLIENT_QUEUE;c->since=web_now();return;
 }
 r=web_http_ipc(&c->request,c->body,sizeof(c->body));if(r){client_error(c,-r);return;}
 c->state=CLIENT_QUEUE;c->since=web_now();
}
static void client_reply(web_client_t*c,char*reply){
 char extra[256],rev[64],csrf[65],session[65],digits[4];const char*p;size_t n;unsigned int code;
 p=field_line(reply,"status");if(strlen(p)<3){client_error(c,503);return;}
 memcpy(digits,p,3);digits[3]=0;if(web_number(digits,599,&code)||code<100){client_error(c,503);return;}
 if(c->asset){if(code!=200)client_error(c,(int)code);else client_asset(c);return;}
 p=field_line(reply,"revision");n=strcspn(p,"\n");if(n>=sizeof(rev))goto bad;memcpy(rev,p,n);rev[n]=0;
 p=field_line(reply,"csrf");n=strcspn(p,"\n");if(n>=sizeof(csrf))goto bad;memcpy(csrf,p,n);csrf[n]=0;
 p=field_line(reply,"session");n=strcspn(p,"\n");if(n>=sizeof(session))goto bad;memcpy(session,p,n);session[n]=0;
 p=field_line(reply,"data");n=strcspn(p,"\n");if(n>3500)goto bad;
 snprintf(c->json,sizeof(c->json),"{\"revision\":\"%s\",\"csrf\":\"%s\",\"data\":%.*s}",rev,csrf,(int)(n?n:2),n?p:"{}");
 extra[0]=0;if(session[0])snprintf(extra,sizeof(extra),"Set-Cookie: %s=%s; Path=/; %sHttpOnly; SameSite=Strict\r\n",c->secure?"session":"session_http",session,c->secure?"Secure; ":"");
 else if(!strcmp(c->request.path,"/api/logout"))snprintf(extra,sizeof(extra),"Set-Cookie: %s=; Path=/; Max-Age=0; %sHttpOnly; SameSite=Strict\r\n",c->secure?"session":"session_http",c->secure?"Secure; ":"");
 client_response(c,(int)code,"application/json",c->json,strlen(c->json),extra,0);goto done;
bad:client_error(c,503);
done:web_clear(csrf,sizeof(csrf));web_clear(session,sizeof(session));web_clear(extra,sizeof(extra));
}
static int client_want(web_client_t*c,int r){
 if(c->secure){if(r==MBEDTLS_ERR_SSL_WANT_READ||r==MBEDTLS_ERR_SSL_WANT_WRITE){c->want=r==MBEDTLS_ERR_SSL_WANT_WRITE;return 1;}}
 else if(r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 1;
 client_free(c);return 0;
}
static void client_step(web_client_t*c){
 int r;size_t n;const unsigned char*p;
 if(c->state==CLIENT_DRAIN){char discard[1024];r=(int)recv(c->fd,discard,sizeof(discard),0);web_clear(discard,sizeof(discard));if(!r||(r<0&&errno!=EAGAIN&&errno!=EINTR))client_free(c);return;}
 if(c->state==CLIENT_TLS){
  r=mbedtls_ssl_handshake_step(&c->ssl);
  if(r){client_want(c,r);return;}
  if(mbedtls_ssl_is_handshake_over(&c->ssl)){c->state=CLIENT_READ;c->since=web_now();c->want=0;}
  else c->want=2; /* next handshake state may require no socket IO */
  return;
 }
 if(c->state==CLIENT_READ){
  if(c->used==WEB_HTTP_MAX){client_error(c,413);return;}
  n=WEB_HTTP_MAX-c->used;if(n>2048)n=2048;
  r=c->secure?mbedtls_ssl_read(&c->ssl,(unsigned char*)c->input+c->used,n):(int)recv(c->fd,c->input+c->used,n,0);
  if(r<=0){client_want(c,r);return;}
  if(!c->used)c->since=web_now();c->used+=(size_t)r;
  r=web_http_parse(c->input,c->used,c->authority,c->secure,&c->request);
  if(r<0){client_error(c,-r);return;}if(r==1)client_route(c);else c->want=0;return;
 }
 if(c->state!=CLIENT_WRITE)return;
 if(c->header_sent<c->header_size){p=(unsigned char*)c->header+c->header_sent;n=c->header_size-c->header_sent;}
 else{p=c->output+c->output_sent;n=c->output_size-c->output_sent;}
 if(n){if(n>4096)n=4096;r=c->secure?mbedtls_ssl_write(&c->ssl,p,n):(int)send(c->fd,p,n,MSG_NOSIGNAL);
  if(r<=0){client_want(c,r);return;}if(c->header_sent<c->header_size)c->header_sent+=(size_t)r;else c->output_sent+=(size_t)r;c->want=1;
 }
 if(c->header_sent==c->header_size&&c->output_sent==c->output_size){
  if(c->close){if(c->secure)mbedtls_ssl_close_notify(&c->ssl);shutdown(c->fd,SHUT_WR);c->state=CLIENT_DRAIN;c->want=0;c->since=web_now();return;}
  web_clear(c->input,sizeof(c->input));web_clear(c->body,sizeof(c->body));web_clear(c->json,sizeof(c->json));web_clear(c->header,sizeof(c->header));web_clear(&c->request,sizeof(c->request));
  c->used=0;c->asset=NULL;c->output=NULL;c->state=CLIENT_READ;c->want=0;c->since=web_now();c->head=0;
 }
}
static int clients_ipc(int broker){
 unsigned int i;int r;ssize_t n;unsigned char b[WEB_FRAME_MAX];
 if(ipc_owner<0){int chosen=-1;uint32_t age=0,now=web_now();
  for(i=0;i<WEB_CLIENTS;++i){unsigned int at=(round_robin+i)%WEB_CLIENTS;web_client_t*c=&clients[at];
   if(c->fd>=0&&c->state==CLIENT_QUEUE&&(chosen<0||now-c->since>age)){chosen=(int)at;age=now-c->since;}}
  if(chosen>=0){web_client_t*c=&clients[chosen];if(web_frame_make(&ipc_tx,c->body)){client_error(c,400);return 0;}
   web_clear(c->body,sizeof(c->body));web_clear(c->input,sizeof(c->input));ipc_owner=chosen;c->state=CLIENT_IPC;c->since=ipc_since=now;}
 }
 if(ipc_owner<0)return 0;
 if(web_now()-ipc_since>=60000U)return -1;
 if(ipc_tx.sent<ipc_tx.total){n=send(broker,ipc_tx.bytes+ipc_tx.sent,ipc_tx.total-ipc_tx.sent,MSG_NOSIGNAL);if(n>0)ipc_tx.sent+=(size_t)n;else if(!n||(errno!=EAGAIN&&errno!=EINTR))return -1;return 0;}
 n=recv(broker,b,sizeof(b),0);if(n<0&&(errno==EAGAIN||errno==EINTR))return 0;if(n<=0)return -1;
 r=web_frame_feed(&ipc_rx,b,(size_t)n);web_clear(b,sizeof(b));if(r<0)return -1;
 if(r==1){web_client_t*c=&clients[ipc_owner];char reply[WEB_BODY_MAX+1];memcpy(reply,ipc_rx.bytes+8,ipc_rx.total-8);reply[ipc_rx.total-8]=0;
  /* Never reassign an outstanding IPC reply to a new socket/session. */
  if(c->fd>=0)client_reply(c,reply);
  web_clear(reply,sizeof(reply));web_clear(&ipc_tx,sizeof(ipc_tx));web_clear(&ipc_rx,sizeof(ipc_rx));ipc_owner=-1;
 }
 return 0;
}
static void clients_run(int*fds,const char**ips,unsigned int https,unsigned int http,int broker,web_certificate_t*cert){
 unsigned int i;uint32_t maintenance=web_now(),accept_at=web_now()-20U;
 for(i=0;i<WEB_CLIENTS;i++)clients[i].fd=-1;
 while(!stopping){
  fd_set reads,writes;struct timeval tv={0,20000};int max=broker,r;uint32_t now=web_now();
  unsigned int delay=web_budget_delay(&client_budget,now,web_cpu_us());
#ifdef WEB_HOST_TEST
  if(client_budget.peak_debit_us>budget_logged_peak){budget_logged_peak=client_budget.peak_debit_us;fprintf(stderr,"web-budget peak_between_checks_us=%u burst_credit_us=%u scope=Web-process-only\n",budget_logged_peak,WEB_CPU_BURST_US);}
#endif
  if(delay){struct timeval pause;pause.tv_sec=0;pause.tv_usec=(delay>20U?20U:delay)*1000U;select(0,NULL,NULL,NULL,&pause);continue;}
  if(now-maintenance>=60000U){maintenance=now;if(cert&&web_certificate_due(cert)){clients_result=28;stopping=1;break;}}
  FD_ZERO(&reads);FD_ZERO(&writes);if(ipc_owner>=0){if(ipc_tx.sent<ipc_tx.total)FD_SET(broker,&writes);else FD_SET(broker,&reads);}
  /* One admission/refusal per 20 ms globally, after active-client work.
   * A full slot still accepts then closes TCP before TLS setup; never evicts. */
  if(now-accept_at>=20U)for(i=0;i<4;i++)if(fds[i]>=0){FD_SET(fds[i],&reads);if(fds[i]>max)max=fds[i];}
  for(i=0;i<WEB_CLIENTS;i++){web_client_t*c=&clients[i];unsigned int timeout;
   if(c->fd<0)continue;
   timeout=c->state==CLIENT_DRAIN?100U:c->state==CLIENT_TLS?15000U:c->state==CLIENT_QUEUE?8000U:c->state==CLIENT_IPC?65000U:c->state==CLIENT_READ&&!c->used?(c->requests?10000U:2000U):5000U;
   if(now-c->since>=timeout){if(c->state==CLIENT_QUEUE)client_error(c,503);else client_free(c);continue;}
   if(c->state==CLIENT_QUEUE||c->state==CLIENT_IPC)continue;
   if(c->want==2|| (c->secure&&c->state==CLIENT_READ&&mbedtls_ssl_check_pending(&c->ssl)))tv.tv_usec=0;
   if(c->want==1)FD_SET(c->fd,&writes);else FD_SET(c->fd,&reads);if(c->fd>max)max=c->fd;
  }
  r=select(max+1,&reads,&writes,NULL,&tv);if(r<0){if(errno==EINTR)continue;break;}
  if(clients_ipc(broker)){stopping=1;break;}
  for(i=0;i<WEB_CLIENTS;i++){web_client_t*c=&clients[(round_robin+i)%WEB_CLIENTS];
   if(web_budget_delay(&client_budget,web_now(),web_cpu_us()))break;
   if(c->fd>=0&& (c->want==2||FD_ISSET(c->fd,&reads)||FD_ISSET(c->fd,&writes)||(c->secure&&c->state==CLIENT_READ&&mbedtls_ssl_check_pending(&c->ssl))))client_step(c);
  }
  round_robin=(round_robin+1)%WEB_CLIENTS;
  for(i=0;i<4;i++)if(fds[i]>=0&&FD_ISSET(fds[i],&reads)){
   unsigned int j;int fd=accept(fds[i],NULL,NULL);web_client_t*c=NULL;accept_at=web_now();if(fd<0)break;
   for(j=0;j<WEB_CLIENTS;j++)if(clients[j].fd<0&&(int)j!=ipc_owner){c=&clients[j];break;}
   if(!c||fd>=FD_SETSIZE||web_nonblock(fd)){close(fd);break;}
   {int on=1;if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&on,sizeof(on))){close(fd);break;}}
   memset(c,0,sizeof(*c));c->fd=fd;c->secure=!(i%2);c->state=c->secure?CLIENT_TLS:CLIENT_READ;c->since=web_now();c->want=0;
   snprintf(c->authority,sizeof(c->authority),"%s",ips[i/2]);if((i%2?http:https)!=(i%2?80U:443U))snprintf(c->authority,sizeof(c->authority),"%s:%u",ips[i/2],i%2?http:https);
   snprintf(c->destination,sizeof(c->destination),"%s",ips[i/2]);if(https!=443)snprintf(c->destination,sizeof(c->destination),"%s:%u",ips[i/2],https);
   mbedtls_ssl_init(&c->ssl);if(c->secure){if(mbedtls_ssl_setup(&c->ssl,&cert->config)){client_free(c);break;}mbedtls_ssl_set_bio(&c->ssl,&c->fd,send_tls,recv_tls,NULL);}
   break;
  }
 }
 for(i=0;i<WEB_CLIENTS;i++)client_free(&clients[i]);web_clear(&ipc_tx,sizeof(ipc_tx));web_clear(&ipc_rx,sizeof(ipc_rx));
#ifdef WEB_HOST_TEST
 fprintf(stderr,"web-budget peak_between_checks_us=%u burst_credit_us=%u scope=Web-process-only\n",client_budget.peak_debit_us,WEB_CPU_BURST_US);
#endif
}
