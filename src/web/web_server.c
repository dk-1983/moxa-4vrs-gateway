#define _GNU_SOURCE
#include "core/platform.h"
#include "web/rng_client.h"
#include "web/web_certificate.h"
#include "web/web_http.h"
#include "web/web_assets.h"
#include "version.h"
#include <arpa/inet.h>
#include <errno.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/resource.h>
static int stopping;
static void stop(int sig){(void)sig;stopping=1;}
static int wait_fd(int fd,int write,uint32_t start,unsigned int timeout){fd_set f;struct timeval t={0,10000};if(stopping||web_now()-start>=timeout||fd<0||fd>=FD_SETSIZE)return -1;FD_ZERO(&f);FD_SET(fd,&f);if(select(fd+1,write?NULL:&f,write?&f:NULL,NULL,&t)<0&&errno!=EINTR)return -1;return 0;}
static int send_tls(void *p,const unsigned char *b,size_t n){ssize_t r=send(*(int*)p,b,n,MSG_NOSIGNAL);if(r<0&&(errno==EAGAIN||errno==EINTR))return MBEDTLS_ERR_SSL_WANT_WRITE;return r<0?MBEDTLS_ERR_SSL_INTERNAL_ERROR:(int)r;}
static int recv_tls(void *p,unsigned char *b,size_t n){ssize_t r=recv(*(int*)p,b,n,0);if(r<0&&(errno==EAGAIN||errno==EINTR))return MBEDTLS_ERR_SSL_WANT_READ;return r<0?MBEDTLS_ERR_SSL_INTERNAL_ERROR:(int)r;}
static int ipc(int fd,const char *request,char *reply){web_frame_t tx,rx;ssize_t n;int r;uint32_t start=web_now();unsigned char b[WEB_FRAME_MAX];memset(&rx,0,sizeof(rx));if(web_frame_make(&tx,request))return -1;while(tx.sent<tx.total){n=send(fd,tx.bytes+tx.sent,tx.total-tx.sent,MSG_NOSIGNAL);if(n>0)tx.sent+=(size_t)n;else if(!n||(errno!=EAGAIN&&errno!=EINTR))return -1;if(wait_fd(fd,1,start,60000))return -1;}web_clear(&tx,sizeof(tx));for(;;){n=recv(fd,b,sizeof(b),0);if(n>0){r=web_frame_feed(&rx,b,(size_t)n);if(r<0)return -1;if(r==1){memcpy(reply,rx.bytes+8,rx.total-8);reply[rx.total-8]=0;web_clear(&rx,sizeof(rx));web_clear(b,sizeof(b));return 0;}}else if(!n||(errno!=EAGAIN&&errno!=EINTR))return -1;if(wait_fd(fd,0,start,60000))return -1;}}
static const char *field_line(char *reply,const char *name){static char empty[]="";char *p=reply;size_t n=strlen(name);while(*p){if(!strncmp(p,name,n)&&p[n]=='=')return p+n+1;p=strchr(p,'\n');if(!p)return empty;++p;}return empty;}
#include "web/web_clients.h"
static int listener(const char *ip,unsigned int port,unsigned int lan){struct sockaddr_in a;const char *device=lan?"" FOURVRS_LAN_PREFIX "1":"" FOURVRS_LAN_PREFIX "0";int fd,on=1;if(!strcmp(ip,"-"))return -2;if(!strcmp(ip,"0.0.0.0")||inet_pton(AF_INET,ip,&a.sin_addr)!=1)return -1;
#ifdef WEB_HOST_TEST
 if(!strncmp(ip,"127.",4))device="lo";
#endif
 fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0||fd>=FD_SETSIZE)return -1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));if(setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,device,strlen(device)+1)){close(fd);return -1;}a.sin_family=AF_INET;a.sin_port=htons((unsigned short)port);if(bind(fd,(struct sockaddr*)&a,sizeof(a))||listen(fd,2)||web_nonblock(fd)){close(fd);return -1;}return fd;}
int main(int argc,char **argv){web_certificate_t cert;int fds[4]={-2,-2,-2,-2},broker=-1,r=1;unsigned int https,http,i,secure;struct sockaddr_un addr;struct rlimit files={32,32},core={0,0};if(argc!=8){fprintf(stderr,"4VRS Web %s: invalid invocation\n",FOURVRS_VERSION);return 2;}if(web_number(argv[7],1,&secure))return 2;if(web_number(argv[5],65535,&https)||!https||web_number(argv[6],65535,&http)||!http||https==http)return 2;signal(SIGPIPE,SIG_IGN);signal(SIGTERM,stop);signal(SIGINT,stop);setrlimit(RLIMIT_NOFILE,&files);setrlimit(RLIMIT_CORE,&core);if(setpriority(PRIO_PROCESS,0,5))return 2;errno=0;r=getpriority(PRIO_PROCESS,0);if(errno||r<5)return 2;
 rng_client_set(3,1);
 web_budget_delay(&client_budget,web_now(),web_cpu_us());
 memset(&cert,0,sizeof(cert));if(secure){r=web_certificate_open(&cert,argv[1],argv[3],argv[4]);if(r)return r;}
 for(i=0;i<4;++i){if((i%2)==secure)continue;fds[i]=listener(argv[3+i/2],i%2?http:https,i/2);if(fds[i]==-1){r=21;goto done;}}
 if(!geteuid()&&(setgroups(0,NULL)||setgid(65534)||setuid(65534))){r=22;goto done;}
 broker=socket(AF_UNIX,SOCK_STREAM,0);if(broker<0){r=23;goto done;}memset(&addr,0,sizeof(addr));addr.sun_family=AF_UNIX;if(strlen(argv[2])>=sizeof(addr.sun_path)){r=23;goto done;}strcpy(addr.sun_path,argv[2]);if(connect(broker,(struct sockaddr*)&addr,sizeof(addr))||web_nonblock(broker)){r=23;goto done;}
 if(secure){char fingerprint[65],body[256],reply[WEB_BODY_MAX+1];if(web_certificate_fingerprint(&cert,fingerprint)){r=27;goto done;}snprintf(body,sizeof(body),"op=certificate\nfingerprint=%s\nuntil=%04d%02d%02d%02d%02d%02d\n",fingerprint,cert.cert.valid_to.year,cert.cert.valid_to.mon,cert.cert.valid_to.day,cert.cert.valid_to.hour,cert.cert.valid_to.min,cert.cert.valid_to.sec);if(ipc(broker,body,reply)||strncmp(reply,"status=200\n",11)){r=23;goto done;}}
 clients_run(fds,(const char**)&argv[3],https,http,broker,secure?&cert:NULL);
 r=clients_result;
done:for(i=0;i<4;++i)if(fds[i]>=0)close(fds[i]);if(broker>=0)close(broker);if(secure)web_certificate_close(&cert);return r;}
