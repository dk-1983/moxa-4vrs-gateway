#define _GNU_SOURCE
#include "web/exec_fds.h"
#include "web/rng_client.h"
#include "web/kdf_worker.h"
#include "web/web_gateway.h"
#include "diagnostics/gateway_diagnostics.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>

static void response(web_gateway_t *w,char *out,size_t n,int status,const char *data){char revision[64];gateway_application_revision(w->app,revision);snprintf(out,n,"status=%d\nrevision=%s\n%s",status,revision,data?data:"");}
static int number(const web_fields_t *f,const char *key,unsigned int max,unsigned int *v){return web_number(web_field(f,key),max,v);}
static void platform_json_text(const char *source,char out[GATEWAY_PLATFORM_TEXT_MAX*6+1]){unsigned int i;size_t at=0;for(i=0;i<GATEWAY_PLATFORM_TEXT_MAX&&source[i];i++){unsigned char c=(unsigned char)source[i];if(c<' '||c=='"'||c=='\\'){snprintf(out+at,7,"\\u%04x",(unsigned)c);at+=6;}else out[at++]=(char)c;}out[at]=0;}
static int validation_error(char *data,size_t n,const char *field,unsigned int lan,const char *code){snprintf(data,n,"data={\"validation\":{\"field\":\"%s\",\"lan\":%u,\"code\":\"%s\"}}\n",field,lan,code);return 422;}
static int network_validation(const gateway_network_settings_t *s,char *data,size_t n){
 unsigned int i,lan=2;unsigned long value;gateway_network_settings_result_t result;char field[24];
 for(i=0;i<2;i++){const char *values[]={s->lan[i].address,s->lan[i].netmask,s->lan[i].gateway};const char *names[]={"address","netmask","gateway"};unsigned int j;
  for(j=0;j<3;j++)if(values[j][0]&&gateway_ipv4_parse(values[j],&value)){snprintf(field,sizeof(field),"%s%u",names[j],i);return validation_error(data,n,field,i+1,"ipv4");}
  if(s->dns[i][0]&&gateway_ipv4_parse(s->dns[i],&value)){snprintf(field,sizeof(field),"dns%u",i);return validation_error(data,n,field,0,"ipv4");}
 }
 result=gateway_network_settings_validate(s,&lan);if(result==GATEWAY_NETWORK_SETTINGS_OK)return 0;
 if(!s->automatic_dns&&s->dns_lan)return validation_error(data,n,"dns_lan",0,"choice");
 if(result==GATEWAY_NETWORK_SETTINGS_OVERLAP)return validation_error(data,n,"address1",2,"overlap");
 if(result==GATEWAY_NETWORK_SETTINGS_DNS){if(s->automatic_dns)return validation_error(data,n,"dns_lan",0,"dns_source");for(i=0;i<2;i++)if(s->dns[i][0]&&!gateway_ipv4_parse(s->dns[i],&value)&&(!value||(value>>24)==127||(value>>24)==0||(value>>24)>=224)){snprintf(field,sizeof(field),"dns%u",i);return validation_error(data,n,field,0,"unicast");}}
 if(lan<2){const char *name=result==GATEWAY_NETWORK_SETTINGS_ADDRESS?"address":result==GATEWAY_NETWORK_SETTINGS_MASK?"netmask":result==GATEWAY_NETWORK_SETTINGS_GATEWAY?"gateway":"mode";const char *code=result==GATEWAY_NETWORK_SETTINGS_ADDRESS?"host_address":result==GATEWAY_NETWORK_SETTINGS_MASK?"mask":result==GATEWAY_NETWORK_SETTINGS_GATEWAY?"gateway":"choice";snprintf(field,sizeof(field),"%s%u",name,lan);return validation_error(data,n,field,lan+1,code);}
 return validation_error(data,n,"default_lan",0,"choice");
}
static void port_json(const gateway_port_config_t *p,unsigned int i,char *out,size_t n){snprintf(out,n,"{\"id\":%u,\"enabled\":%u,\"mode\":%u,\"baud\":%lu,\"data_bits\":%u,\"parity\":%u,\"stop_bits\":%u,\"transport\":%u,\"bind\":\"%s\",\"port\":%u,\"special_enabled\":%u,\"special_baud\":%lu}",i+1,(unsigned)p->enabled,(unsigned)p->mode,p->baud,p->data_bits,(unsigned)p->parity,p->stop_bits,(unsigned)p->transport,p->bind_address,p->endpoint_port,(unsigned)p->special_baud_enabled,p->special_baud);}
int web_gateway_request(web_gateway_t *w,const char *body,size_t length,char *out,size_t n,uint32_t now){web_fields_t f;const char *op;gateway_application_t *a=w->app;gateway_persistent_config_t c;unsigned int i,v,id;char data[3500],token[64];int r=400,write,detail;
 if(web_fields_parse(&f,body,length)){response(w,out,n,400,NULL);return 400;}op=web_field(&f,"op");data[0]=0;
 if(!strcmp(op,"certificate")){unsigned char digest[32];const char *until=web_field(&f,"until");size_t j;if(web_fields_only(&f,"|op||fingerprint||until|")||web_unhex(web_field(&f,"fingerprint"),digest,32)||strlen(until)!=14)goto done;for(j=0;j<14;j++)if(until[j]<'0'||until[j]>'9')goto done;strcpy(a->web.fingerprint,web_field(&f,"fingerprint"));strcpy(a->web.certificate_until,until);r=200;goto done;}
 if(!strcmp(op,"hello")){if(f.count!=1)r=400;else{snprintf(data,sizeof(data),"data={\"configured\":%s,\"enrollment\":%s}\n",w->security.administrator?"true":"false",w->security.code[0]?"true":"false");r=200;}goto done;}
 if(!strcmp(op,"recover")){if(f.count!=1)goto done;if(a->web.recovery_prompt||w->security.code[0]){r=202;goto done;}if(!w->security.administrator||w->security.save_failed||!a->web.recovery_available||w->security.worker){r=423;goto done;}if(w->recovery_rate_at&&now-w->recovery_rate_at<60000U){r=429;goto done;}w->recovery_rate_at=w->recovery_at=now;a->web.recovery_prompt=1;r=202;goto done;}
 if(!strcmp(op,"enroll")||!strcmp(op,"login")){if(web_fields_only(&f,"|op||password||code|"))goto done;r=web_security_begin(&w->security,!strcmp(op,"enroll"),web_field(&f,"code"),web_field(&f,"password"),now);web_clear(&f,sizeof(f));if(!r){w->auth_pending=1;return 0;}goto done;}
 detail=strlen(op)==7&&!strncmp(op,"detail",6)&&op[6]>='1'&&op[6]<='8';
 write=!detail&&strcmp(op,"help")&&strcmp(op,"overview")&&strcmp(op,"ports")&&strcmp(op,"network")&&strcmp(op,"diagnostics")&&strcmp(op,"system")&&strcmp(op,"about");
 if(!web_security_authorized(&w->security,web_field(&f,"session"),web_field(&f,"csrf"),write,now)){r=401;goto done;}
 if(!strcmp(op,"logout")){web_clear(w->security.token,sizeof(w->security.token));web_clear(w->security.csrf,sizeof(w->security.csrf));r=200;goto done;}
 if(!write){if(web_fields_only(&f,"|op||session|"))goto done;r=200;
  if(detail){gateway_coordinator_health_t h;const gateway_port_health_t *p;gateway_coordinator_health(&a->coordinator,now,&h);p=&h.gateway.port[op[6]-'1'];snprintf(data,sizeof(data),"data={\"id\":%u,\"lifecycle\":%u,\"error\":%u,\"accepted\":%u,\"completed\":%u,\"timeouts\":%u,\"recoveries\":%u,\"stale\":%u,\"queue\":%u,\"queue_high_water\":%u,\"crc\":%u,\"framing\":%u,\"unit_mismatch\":%u,\"function_mismatch\":%u,\"garbage\":%u,\"generation\":%u,\"raw_network_received\":%u,\"raw_network_sent\":%u,\"raw_serial_read\":%u,\"raw_serial_written\":%u}\n",(unsigned)(op[6]-'0'),(unsigned)p->lifecycle,(unsigned)p->last_error,p->transactions.accepted,p->transactions.completed,p->transactions.timeouts,p->transactions.recoveries,p->transactions.stale_responses,p->transactions.queue_depth,p->transactions.queue_high_water,p->transport.crc_failures,p->transport.framing_failures,p->transport.unit_mismatches,p->transport.function_mismatches,p->transport.leading_garbage,p->config_generation,p->transport.raw_network_received,p->transport.raw_network_sent,p->transport.raw_serial_read,p->transport.raw_serial_written);}
  else if(!strcmp(op,"help"))strcpy(data,"data={}\n");
  else if(!strcmp(op,"about")){char kernel[GATEWAY_PLATFORM_TEXT_MAX*6+1],architecture[GATEWAY_PLATFORM_TEXT_MAX*6+1];platform_json_text(a->platform.kernel,kernel);platform_json_text(a->platform.architecture,architecture);snprintf(data,sizeof(data),"data={\"version\":\"%s\",\"device\":\"Moxa UC-7420-LX Plus\",\"kernel\":\"%s\",\"architecture\":\"%s\",\"word_bits\":%u,\"big_endian\":%u}\n",gateway_product_metadata()->version,kernel,architecture,a->platform.word_bits,a->platform.big_endian);}
  else if(!strcmp(op,"diagnostics")){size_t at=6;unsigned int count=gateway_coordinator_event_count(&a->coordinator);strcpy(data,"data=[");for(i=0;i<count;++i){gateway_startup_event_t e;gateway_coordinator_event(&a->coordinator,i,&e);at+=(size_t)snprintf(data+at,sizeof(data)-at,"%s{\"sequence\":%u,\"stage\":%u,\"result\":%u,\"progress\":%u,\"port\":%u,\"error\":%u}",i?",":"",e.sequence,(unsigned)e.stage,(unsigned)e.result,e.progress_percent,e.port_index==GATEWAY_NO_PORT?0:e.port_index+1,(unsigned)e.error);}snprintf(data+at,sizeof(data)-at,"]\n");}
  else if(!strcmp(op,"ports")){size_t at=0;char p[384];strcpy(data,"data=[");at=6;for(i=0;i<8;++i){port_json(&a->coordinator.selected.ports[i],i,p,sizeof(p));at+=(size_t)snprintf(data+at,sizeof(data)-at,"%s%s",i?",":"",p);}snprintf(data+at,sizeof(data)-at,"]\n");}
  else if(!strcmp(op,"network")){const gateway_network_settings_t *s=&a->network.confirmed.settings;snprintf(data,sizeof(data),"data={\"available\":%s,\"epoch\":\"%s\",\"operation\":%u,\"remaining_seconds\":%u,\"state\":%u,\"lan\":[{\"mode\":%u,\"address\":\"%s\",\"netmask\":\"%s\",\"gateway\":\"%s\"},{\"mode\":%u,\"address\":\"%s\",\"netmask\":\"%s\",\"gateway\":\"%s\"}],\"default_lan\":%u,\"automatic_dns\":%u,\"dns_lan\":%u,\"dns\":[\"%s\",\"%s\"],\"keep_allowed\":%s}\n",a->network.available?"true":"false",a->epoch,a->network_operation,a->network.status.state==GATEWAY_NETWORK_WAIT_CONFIRM&&(int32_t)(a->network.status.deadline-now)>0?(unsigned)(a->network.status.deadline-now)/1000U:0U,(unsigned)a->network.status.state,(unsigned)s->lan[0].mode,s->lan[0].address,s->lan[0].netmask,s->lan[0].gateway,(unsigned)s->lan[1].mode,s->lan[1].address,s->lan[1].netmask,s->lan[1].gateway,s->default_lan,s->automatic_dns,s->dns_lan,s->dns[0],s->dns[1],a->network.status.state==GATEWAY_NETWORK_WAIT_CONFIRM&&a->network.binding_ack?"true":"false");{size_t at=strlen(data)-2;const gateway_network_observation_t *o=&a->network.observed;snprintf(data+at,sizeof(data)-at,",\"rollback_reason\":%u,\"provider_error\":%d,\"observed_valid\":%s,\"observed\":{\"lan\":[{\"address\":\"%s\",\"netmask\":\"%s\",\"link\":%u},{\"address\":\"%s\",\"netmask\":\"%s\",\"link\":%u}],\"default_lan\":%u,\"gateway\":\"%s\",\"dns\":[\"%s\",\"%s\"]}}\n",(unsigned)a->network.status.rollback_reason,a->network.status.error_code,a->network.observed_valid&&!a->network.observation_error?"true":"false",o->lan[0].address,o->lan[0].netmask,o->lan[0].link,o->lan[1].address,o->lan[1].netmask,o->lan[1].link,o->default_lan,o->gateway,o->dns[0],o->dns[1]);}}
  else {gateway_coordinator_health_t health;gateway_system_time_t t;struct sysinfo si;unsigned int tcp=0;gateway_coordinator_health(&a->coordinator,now,&health);for(i=0;i<8;i++)if(health.gateway.port[i].config.transport==0||health.gateway.port[i].config.transport==3)tcp+=health.gateway.port[i].connected_clients;memset(&si,0,sizeof(si));sysinfo(&si);memset(&t,0,sizeof(t));gateway_application_system_time_get(a,&t);snprintf(data,sizeof(data),"data={\"product\":\"4VRS Gateway\",\"version\":\"%s\",\"device\":\"Moxa UC-7420-LX Plus\",\"state\":%u,\"ports_ready\":%u,\"ports_enabled\":%u,\"tcp_clients\":%u,\"alarms\":%u,\"uptime\":%lu,\"gateway_uptime_ms\":%lu,\"clock_trust\":%u,\"web_ips\":[\"%s\",\"%s\"],\"fingerprint\":\"%s\",\"certificate_until\":\"%s\",\"web_state\":%u,\"web_error\":%u,\"web_enabled\":%u,\"web_interface\":%u,\"web_protocol\":%u,\"transaction\":%u,\"backlight\":%u,\"time\":[%u,%u,%u,%u,%u,%u],\"ntp_enabled\":%u,\"ntp_interval\":%u,\"ntp_server\":\"%s\",\"ntp_result\":%u}\n",gateway_product_metadata()->version,(unsigned)a->process_state,health.ready_ports,health.enabled_ports,tcp,health.error_ports,(unsigned long)si.uptime,(unsigned long)health.uptime,(unsigned)a->time.trust,w->addresses[0],w->addresses[1],a->web.fingerprint,a->web.certificate_until,a->web.state,a->web.error,a->coordinator.selected.settings.web_enabled,a->coordinator.selected.settings.web_interface,a->coordinator.selected.settings.web_protocol,(unsigned)a->configuration_transaction.state,a->coordinator.selected.settings.backlight_on,t.year,t.month,t.day,t.hour,t.minute,t.second,(unsigned)a->coordinator.selected.settings.ntp_enabled,a->coordinator.selected.settings.ntp_interval_hours,a->coordinator.selected.settings.ntp_server,(unsigned)a->time.last_ntp_result);}
  goto done;
 }
 if(!strcmp(op,"keep")||!strcmp(op,"revert")){if(web_fields_only(&f,"|op||session||csrf||epoch||operation|"))goto done;if(strcmp(a->epoch,web_field(&f,"epoch"))||number(&f,"operation",0xffffffffU,&v)||v!=a->network_operation){r=409;goto done;}r=(!strcmp(op,"keep")?gateway_application_network_keep(a):gateway_application_network_revert(a))?409:202;goto done;}
 if(gateway_application_revision_check(a,web_field(&f,"revision"))){r=409;goto done;}
 if(!strcmp(op,"password")){if(web_fields_only(&f,"|op||session||csrf||revision||current||password||repeat|"))goto done;r=web_security_change(&w->security,web_field(&f,"current"),web_field(&f,"password"),web_field(&f,"repeat"),now);web_clear(&f,sizeof(f));if(!r){w->auth_pending=1;return 0;}goto done;}
 c=a->coordinator.selected;
 if(!strcmp(op,"port")){gateway_port_config_t *p;if(web_fields_only(&f,"|op||session||csrf||revision||id||enabled||mode||baud||data_bits||parity||stop_bits||transport||bind||port||special_enabled||special_baud|")||number(&f,"id",8,&id)||!id)goto done;p=&c.ports[id-1];
#define PNUM(key,max,field) if(number(&f,key,max,&v)){r=validation_error(data,sizeof(data),key,0,!strcmp(key,"port")?"port":"number");goto done;} p->field=v
 PNUM("enabled",1,enabled);PNUM("mode",3,mode);PNUM("baud",1000000,baud);PNUM("data_bits",8,data_bits);PNUM("parity",2,parity);PNUM("stop_bits",2,stop_bits);PNUM("transport",4,transport);PNUM("port",65535,endpoint_port);PNUM("special_enabled",1,special_baud_enabled);PNUM("special_baud",1000000,special_baud);
#undef PNUM
 {unsigned long address;if(strlen(web_field(&f,"bind"))>15||gateway_ipv4_parse(web_field(&f,"bind"),&address)){r=validation_error(data,sizeof(data),"bind",0,"ipv4");goto done;}}
 if(p->enabled&&!p->endpoint_port){r=validation_error(data,sizeof(data),"port",0,"port");goto done;}
 if(p->data_bits<5){r=validation_error(data,sizeof(data),"data_bits",0,"data_bits");goto done;}
 if(!p->stop_bits){r=validation_error(data,sizeof(data),"stop_bits",0,"stop_bits");goto done;}
 {int j,found=0;for(j=0;j<FOURVRS_BAUD_COUNT;j++)if(baud_value(j)==p->baud)found=1;if(!p->special_baud_enabled&&!found){r=validation_error(data,sizeof(data),"baud",0,"baud");goto done;}if(p->special_baud_enabled&&!p->special_baud){r=validation_error(data,sizeof(data),"special_baud",0,"number");goto done;}}
 strcpy(p->bind_address,web_field(&f,"bind"));++p->revision;
 {gateway_error_t errors[8];if(gateway_configuration_validate(c.ports,errors)){r=validation_error(data,sizeof(data),"port",0,errors[id-1]==GATEWAY_ERROR_DUPLICATE_ENDPOINT?"duplicate_endpoint":"port_configuration");goto done;}}
 r=gateway_application_request_configuration(a,&c)?503:202;
 }else if(!strcmp(op,"web")){unsigned int protocol=c.settings.web_protocol;if(w->switch_pending){r=423;goto done;}if(web_fields_only(&f,"|op||session||csrf||revision||enabled||interface||protocol|")||number(&f,"enabled",1,&v)||number(&f,"interface",2,&id)||(*web_field(&f,"protocol")&&number(&f,"protocol",1,&protocol)))goto done;c.schema_version=3;c.settings.web_enabled=v;c.settings.web_interface=id;c.settings.web_protocol=protocol;r=gateway_application_request_configuration(a,&c)?422:202;}
 else if(!strcmp(op,"backlight")){if(web_fields_only(&f,"|op||session||csrf||revision||enabled|")||number(&f,"enabled",1,&v))goto done;r=gateway_application_backlight_set(a,v)?422:202;}
 else if(!strcmp(op,"ntp")){if(web_fields_only(&f,"|op||session||csrf||revision||enabled||interval||server|")||number(&f,"enabled",1,&v)||number(&f,"interval",24,&id)||strlen(web_field(&f,"server"))>=sizeof(c.settings.ntp_server))goto done;c.settings.ntp_enabled=v;c.settings.ntp_interval_hours=id;strcpy(c.settings.ntp_server,web_field(&f,"server"));r=gateway_application_request_configuration(a,&c)?422:202;}
 else if(!strcmp(op,"ntptest")){if(web_fields_only(&f,"|op||session||csrf||revision||enabled|")||number(&f,"enabled",1,&v))goto done;r=gateway_application_ntp_test(a,v)?422:200;}
 else if(!strcmp(op,"time")){gateway_system_time_t t;const char *names[]={"year","month","day","hour","minute","second"};unsigned int *values[]={&t.year,&t.month,&t.day,&t.hour,&t.minute,&t.second};if(web_fields_only(&f,"|op||session||csrf||revision||year||month||day||hour||minute||second|"))goto done;for(i=0;i<6;++i)if(number(&f,names[i],2037,values[i]))goto done;r=gateway_application_system_time_set(a,&t)==GATEWAY_SYSTEM_TIME_OK?200:422;}
 else if(!strcmp(op,"network_apply")||!strcmp(op,"network_review")){
 gateway_network_settings_t settings=a->network.confirmed.settings;const char *keys[]={"mode0","address0","netmask0","gateway0","mode1","address1","netmask1","gateway1"};
 if(web_fields_only(&f,"|op||session||csrf||revision||mode0||address0||netmask0||gateway0||mode1||address1||netmask1||gateway1||default_lan||automatic_dns||dns_lan||dns0||dns1|"))goto done;
 for(i=0;i<2;i++){unsigned int j;char *targets[]={settings.lan[i].address,settings.lan[i].netmask,settings.lan[i].gateway};if(number(&f,keys[i*4],1,&v)){r=validation_error(data,sizeof(data),keys[i*4],i+1,"choice");goto done;}settings.lan[i].mode=v;for(j=0;j<3;j++){const char *key=keys[i*4+j+1];if(strlen(web_field(&f,key))>15){r=validation_error(data,sizeof(data),key,i+1,"ipv4");goto done;}strcpy(targets[j],web_field(&f,key));}}
 {const char *names[]={"default_lan","automatic_dns","dns_lan"};unsigned int *values[]={&settings.default_lan,&settings.automatic_dns,&settings.dns_lan};for(i=0;i<3;i++)if(number(&f,names[i],i==1?1:2,values[i])){r=validation_error(data,sizeof(data),names[i],0,"choice");goto done;}}
 for(i=0;i<2;i++){const char *key=i?"dns1":"dns0";if(strlen(web_field(&f,key))>15){r=validation_error(data,sizeof(data),key,0,"ipv4");goto done;}strcpy(settings.dns[i],web_field(&f,key));}
 r=network_validation(&settings,data,sizeof(data));if(r)goto done;
 if(!strcmp(op,"network_review")){r=200;goto done;}
 if(gateway_network_runtime_busy(&a->network)||a->configuration_transaction.state==GATEWAY_CONFIG_TX_ACTIVATING||a->configuration_transaction.state==GATEWAY_CONFIG_TX_PROMOTING||a->configuration_transaction.state==GATEWAY_CONFIG_TX_ROLLING_BACK){r=423;strcpy(data,"data={\"failure\":{\"code\":\"busy\"}}\n");goto done;}
 if(gateway_application_network_apply(a,&settings)){r=503;strcpy(data,"data={\"failure\":{\"code\":\"apply_unavailable\"}}\n");}else{r=202;snprintf(data,sizeof(data),"data={\"epoch\":\"%s\",\"operation\":%u}\n",a->epoch,a->network_operation);}
 }
 else if(!strcmp(op,"gateway_stop")){if(web_fields_only(&f,"|op||session||csrf||revision|"))goto done;gateway_application_request_stop(a);r=202;}
done:gateway_application_revision(a,token);response(w,out,n,r,data);web_clear(&f,sizeof(f));return r;
}

static void close_peer(web_gateway_t *w){if(w->peer>=0)close(w->peer);w->peer=-1;memset(&w->incoming,0,sizeof(w->incoming));memset(&w->outgoing,0,sizeof(w->outgoing));if(w->auth_pending){web_security_cancel_pending(&w->security);w->auth_pending=0;}}
/* Queue orderly stop before closing IPC: EOF must not race ahead of SIGTERM. */
static void rng_stop(web_gateway_t*w){if(w->rng_pid>0){kill(w->rng_pid,SIGTERM);w->rng_stopping=1;}rng_client_close();if(w->rng_web>=0)close(w->rng_web);w->rng_web=-1;if(w->rng_attaching&&w->rng_pending_fd>=0)close(w->rng_pending_fd);w->rng_pending_fd=-1;w->rng_attaching=0;w->rng_epoch_set=0;web_security_cancel(&w->security);}
static int rng_launch(web_gateway_t*w){int a[2],b[2],x,y,pid;char binary[512],path[512],*slash;sigset_t block,previous;
 strcpy(binary,w->binary);slash=strrchr(binary,'/');if(!slash)return -1;strcpy(slash+1,"4vrs-rng");
 strcpy(path,"/var/hda/4vrs-rng");
#ifdef WEB_HOST_TEST
 snprintf(path,sizeof(path),"%s/rng",w->directory);
#endif
 if(socketpair(AF_UNIX,SOCK_STREAM,0,a))return -1;if(socketpair(AF_UNIX,SOCK_STREAM,0,b)){close(a[0]);close(a[1]);return -1;}
 x=fcntl(a[1],F_DUPFD,10);y=fcntl(b[1],F_DUPFD,10);close(a[1]);close(b[1]);if(x<0||y<0){close(a[0]);close(b[0]);if(x>=0)close(x);if(y>=0)close(y);return -1;}
 sigemptyset(&block);sigaddset(&block,SIGTERM);sigaddset(&block,SIGINT);
 if(sigprocmask(SIG_BLOCK,&block,&previous)){close(x);close(y);close(a[0]);close(b[0]);return -1;}
 pid=fork();if(!pid){if(dup2(x,3)<0||dup2(y,4)<0||web_exec_close_from(5))_exit(127);{int nullfd=open("/dev/null",O_RDWR);if(nullfd<0)_exit(127);dup2(nullfd,0);dup2(nullfd,1);if(nullfd>4)close(nullfd);}execl(binary,binary,"serve",path,(char*)NULL);_exit(127);}
 /* The child keeps the mask until the broker installs handlers before NV I/O. */
 if(sigprocmask(SIG_SETMASK,&previous,NULL)&&pid>0)kill(pid,SIGTERM);
 close(x);close(y);if(pid<0){close(a[0]);close(b[0]);return -1;}
 w->rng_pid=pid;w->rng_web=b[0];rng_client_set(a[0],0);return 0;
}
static int retry_init(web_gateway_t *w){char dir[256],binary[256];gateway_application_t *a=w->app;unsigned int https=w->https_port,http=w->http_port;strcpy(dir,w->directory);strcpy(binary,w->binary);return web_gateway_init(w,a,dir,binary,https,http);}
int web_gateway_local(void *context,unsigned int action){web_gateway_t *w=context;int r;if(!w->initialized&&retry_init(w))return -1;w->app->web.recovery_prompt=0;r=web_security_local(&w->security,action,web_now());strcpy(w->app->web.code,w->security.code);w->app->web.enrollment=w->security.code[0]!=0;w->app->web.seconds_left=w->app->web.enrollment?WEB_CODE_MS/1000:0;if(r)w->app->web.error=1;return r;}
int web_gateway_init(web_gateway_t *w,gateway_application_t *a,const char *dir,const char *binary,unsigned int https,unsigned int http){char path[512];struct stat st;struct rlimit core={0,0};setrlimit(RLIMIT_CORE,&core);memset(w,0,sizeof(*w));w->listener=w->peer=w->rng_web=-1;web_ipc_endpoint_init(&w->ipc_endpoint);w->app=a;w->protocol_seen=a->coordinator.selected.settings.web_protocol;w->https_port=https;w->http_port=http;w->peer_uid=geteuid()?geteuid():65534;if(strlen(dir)>240||strlen(binary)>255)return -1;strcpy(w->directory,dir);strcpy(w->binary,binary);snprintf(w->socket_path,sizeof(w->socket_path),"/tmp/4vrs-web-ipc/%lu.sock",(unsigned long)getpid());a->web.local=web_gateway_local;a->web.context=w;

#ifndef WEB_HOST_TEST
 {struct stat parent,cf;if(strncmp(dir,"/var/hda/",9)||stat("/var",&parent)||stat("/var/hda",&cf)||parent.st_dev==cf.st_dev)return -1;}
#endif
 if(mkdir(dir,0700)&&errno!=EEXIST)return -1;if(lstat(dir,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077))return -1;
 snprintf(path,sizeof(path),"%s/admin",dir);web_security_init(&w->security,path);{char worker[512],*slash;strcpy(worker,binary);slash=strrchr(worker,'/');if(!slash)return -1;strcpy(slash+1,"4vrs-kdf");web_security_worker_path(worker);}w->initialized=1;return 0;}
static int open_ipc(web_gateway_t *w){
 unsigned long id=(unsigned long)getpid();int fd;
#ifdef WEB_HOST_TEST
 if(getenv("WEB_IPC_TEST_ID"))id=strtoul(getenv("WEB_IPC_TEST_ID"),NULL,10);
#endif
 fd=web_ipc_endpoint_open(&w->ipc_endpoint,"/tmp/4vrs-web-ipc",id);
 if(fd<0)return -1;strcpy(w->socket_path,w->ipc_endpoint.path);w->listener=fd;return 0;
}
static void stop_service(web_gateway_t *w){if(w->pid>0&&!w->stopping){kill(w->pid,SIGTERM);w->stopping=1;w->started_at=web_now();}close_peer(w);web_ipc_endpoint_close(&w->ipc_endpoint,w->listener);w->listener=-1;}
void web_gateway_close(web_gateway_t *w){if(!w->initialized)return;rng_stop(w);if(w->rng_pid>0)waitpid(w->rng_pid,NULL,0);w->rng_pid=0;web_security_cancel(&w->security);stop_service(w);if(w->pid>0){kill(w->pid,SIGKILL);waitpid(w->pid,NULL,0);w->pid=0;}}
unsigned int web_gateway_network_error(const gateway_network_runtime_t *network,unsigned int selected,char addresses[2][16])
{
 unsigned int i;memset(addresses,0,32);
 if(!network->observed_valid)return 60;
 if(network->observation_error)return 61;
 if(selected>2U)return 2;
 for(i=0;i<2U;++i)if(selected==2U||selected==i){
  const gateway_lan_observation_t *lan=&network->observed.lan[i];
  if(!lan->up||!lan->link||!lan->address[0]||!strcmp(lan->address,"0.0.0.0"))return 2;
  strcpy(addresses[i],lan->address);
 }
 return 0;
}
void web_gateway_step(web_gateway_t *w){gateway_application_t *a=w->app;uint32_t now=web_now();char addresses[2][16]={{0}},https[8],http[8],protocol[2],reply[WEB_BODY_MAX+1];int enabled=a->coordinator.selected.settings.web_enabled&&!a->run_control.stop_requested;unsigned int i,select=a->coordinator.selected.settings.web_interface,network_error;int status,r;ssize_t n;unsigned char buffer[WEB_FRAME_MAX];
 /* Revoke at the first observation of a saved protocol change, before
  * servicing further IPC. Let the already accepted response drain before stop. */
 if(w->initialized&&w->protocol_seen!=a->coordinator.selected.settings.web_protocol){
  if(!w->switch_pending&&w->peer>=0&&!w->stopping){w->switch_pending=1;w->switch_from=w->protocol_seen;w->switch_at=now;}
  w->protocol_seen=a->coordinator.selected.settings.web_protocol;
  web_security_cancel(&w->security);w->auth_pending=0;
 }
 if(w->switch_pending&&(w->switch_failed||now-w->switch_at>=65000U)){
  gateway_persistent_config_t restore=a->coordinator.selected;restore.schema_version=3;restore.settings.web_protocol=w->switch_from;
  if(!gateway_application_request_configuration(a,&restore)){w->switch_pending=0;w->switch_failed=0;w->retry_at=0;}
 }
 if(w->pid>0&&!w->stopping&&w->launch_protocol!=w->protocol_seen&&!w->outgoing.total){stop_service(w);w->retry_at=now;}
 if(!w->initialized){if(!w->retry_at||(int32_t)(now-w->retry_at)>=0){if(retry_init(w)){w->retry_at=now+60000U;}else return;}a->web.state=3;a->web.error=1;return;}
 if(w->rng_pid>0&&waitpid(w->rng_pid,&status,WNOHANG)==w->rng_pid){w->rng_pid=0;rng_stop(w);stop_service(w);w->rng_retry=now+60000U;if(!w->rng_stopping||!WIFEXITED(status)||WEXITSTATUS(status)!=0){a->web.error=WIFEXITED(status)?30U+(unsigned int)WEXITSTATUS(status):39U;w->rng_fault=1;}w->rng_stopping=0;}
 /* An RNG failure never triggers another automatic CF write. Gateway and
  * serial processing continue; explicit service and Gateway restart clear this
  * in-memory latch, while the broker also enforces its durable crash intent. */
 if(!w->rng_fault&&!w->rng_pid&&enabled&&(!w->rng_retry||(int32_t)(now-w->rng_retry)>=0)){if(rng_launch(w))w->rng_retry=now+60000U;}
 if(!w->rng_fault&&w->rng_pid>0&&!w->rng_stopping&&rng_client_step(now)<0){rng_stop(w);w->rng_stopping=0;w->rng_fault=1;stop_service(w);a->web.error=39;}
 if(w->stopping&&w->pid>0&&waitpid(w->pid,&status,WNOHANG)==w->pid){w->pid=0;w->stopping=0;}
 if(w->stopping&&now-w->started_at>1000U&&w->pid>0)kill(w->pid,SIGKILL);
 if(!enabled){rng_stop(w);if(!a->run_control.stop_requested&&a->configuration_transaction.state==GATEWAY_CONFIG_TX_PROMOTING&&a->configuration_transaction.candidate.settings.web_enabled){a->web.state=1;return;}a->web.recovery_prompt=0;stop_service(w);a->web.state=w->pid?1:0;a->web.urls[0][0]=a->web.urls[1][0]=0;w->failures=0;return;}
 if(!rng_client_ready()){a->web.state=3;if(!a->web.error)a->web.error=31;return;}
 if(w->peer>=0&&!w->stopping){a->web.state=2;a->web.error=0;}
 if(!w->rng_epoch_set){unsigned char epoch[16];if(web_random(epoch,16))return;web_hex(epoch,16,a->epoch);web_clear(epoch,16);gateway_application_revision_advance(a);w->rng_epoch_set=1;}
 if(a->web.recovery_prompt&&now-w->recovery_at>=60000U)a->web.recovery_prompt=0;
 web_security_tick(&w->security,now);a->web.enrollment=w->security.code[0]!=0;strcpy(a->web.code,w->security.code);a->web.seconds_left=a->web.enrollment?(WEB_CODE_MS-(now-w->security.code_at))/1000:0;
 if(w->pid>0&&waitpid(w->pid,&status,WNOHANG)==w->pid){w->pid=0;close_peer(w);if(w->switch_pending)w->switch_failed=1;a->web.state=3;a->web.error=WIFEXITED(status)?(unsigned int)WEXITSTATUS(status):9;if(w->stopping){w->stopping=0;if((int32_t)(w->retry_at-now)<0)w->retry_at=now;}else if(WIFEXITED(status)&&WEXITSTATUS(status)==28){w->retry_at=now-w->started_at<60000U?w->started_at+60000U:now;}else{if(w->failures<6)++w->failures;w->retry_at=now+(1000U<<w->failures);}}
 if(!rng_client_ready())return;
 if(a->process_state!=GATEWAY_PROCESS_RUNNING)return;
 network_error=web_gateway_network_error(&a->network,select,addresses);
 if(network_error){if(w->pid||w->peer>=0)stop_service(w);a->web.state=3;a->web.error=network_error;return;}

#ifdef WEB_HOST_TEST
 for(i=0;i<2;++i)if(addresses[i][0]&&w->test_addresses[i][0])strcpy(addresses[i],w->test_addresses[i]);
#endif
 if((w->pid||w->listener>=0)&&memcmp(addresses,w->addresses,sizeof(addresses))){stop_service(w);w->failures=0;w->retry_at=now+2000U;}
 /* At most one launch/certificate publication per ten seconds, including
  * address churn. Leave time for the network Keep confirmation deadline. */
 if(w->pid<=0&&(!w->retry_at||(int32_t)(now-w->retry_at)>=0)){
  if(w->last_launch_at&&now-w->last_launch_at<10000U)return;
  if(w->protocol_seen&&a->time.trust!=GATEWAY_TIME_SYNCED&&a->time.trust!=GATEWAY_TIME_MANUAL&&a->time.trust!=GATEWAY_TIME_HOLDOVER){a->web.state=3;a->web.error=3;if(w->switch_pending)w->switch_failed=1;return;}
  if(w->listener<0&&open_ipc(w)){a->web.state=3;a->web.error=4;w->retry_at=now+60000U;return;}
  if(!w->rng_attaching){int channel[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,channel))return;if(rng_client_attach(channel[1])){close(channel[0]);close(channel[1]);rng_stop(w);return;}close(channel[1]);w->rng_pending_fd=channel[0];w->rng_attaching=1;return;}
  if(w->rng_web>=0)close(w->rng_web);w->rng_web=w->rng_pending_fd;w->rng_pending_fd=-1;w->rng_attaching=0;
  memcpy(w->addresses,addresses,sizeof(addresses));snprintf(https,sizeof(https),"%u",w->https_port);snprintf(http,sizeof(http),"%u",w->http_port);
  w->launch_protocol=w->protocol_seen;protocol[0]=(char)('0'+w->launch_protocol);protocol[1]=0;w->last_launch_at=now;w->pid=fork();if(!w->pid){if(setsid()<0||dup2(w->rng_web,3)<0||web_exec_close_from(4))_exit(127);execl(w->binary,w->binary,w->directory,w->socket_path,addresses[0][0]?addresses[0]:"-",addresses[1][0]?addresses[1]:"-",https,http,protocol,(char*)NULL);_exit(127);}if(w->pid<0){w->pid=0;w->retry_at=now+60000U;a->web.state=3;a->web.error=5;return;}a->web.state=1;w->started_at=now;
 }
 if(w->listener>=0){struct ucred cred;socklen_t len=sizeof(cred);int fd=accept(w->listener,NULL,NULL);if(fd>=0){if(w->peer>=0||getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&cred,&len)||cred.pid!=w->pid||cred.uid!=w->peer_uid||web_nonblock(fd))close(fd);else{w->peer=fd;w->peer_at=now;if(w->launch_protocol==w->protocol_seen){w->switch_pending=0;w->switch_failed=0;}a->web.state=2;a->web.error=0;if(!w->auto_code_issued&&!w->security.administrator&&!w->security.save_failed){w->auto_code_issued=1;if(!web_gateway_local(w,1))a->web.show_code=1;}for(i=0;i<2;++i)if(addresses[i][0])snprintf(a->web.urls[i],40,"%s://%s",w->launch_protocol?"https":"http",addresses[i]);else a->web.urls[i][0]=0;}}}
 if(w->pid>0&&a->web.state==1&&now-w->started_at>60000U){stop_service(w);a->web.error=6;}
 if(w->peer<0)return;
 if(w->auth_pending){r=web_security_poll(&w->security,now);if(!r)return;w->auth_pending=0;if(r==200){char data[256];snprintf(data,sizeof(data),"session=%s\ncsrf=%s\ndata={}\n",w->security.token,w->security.csrf);response(w,reply,sizeof(reply),200,data);}else response(w,reply,sizeof(reply),r,NULL);web_frame_make(&w->outgoing,reply);web_clear(reply,sizeof(reply));}
 if(w->outgoing.total){n=send(w->peer,w->outgoing.bytes+w->outgoing.sent,w->outgoing.total-w->outgoing.sent,MSG_NOSIGNAL);if(n>0)w->outgoing.sent+=(size_t)n;else if(n<0&&errno!=EAGAIN&&errno!=EINTR){close_peer(w);return;}if(w->outgoing.sent==w->outgoing.total){web_clear(&w->outgoing,sizeof(w->outgoing));web_clear(&w->incoming,sizeof(w->incoming));}else if(now-w->peer_at>60000U)close_peer(w);return;}
 n=recv(w->peer,buffer,sizeof(buffer),0);if(!n){close_peer(w);return;}if(n<0){if(errno!=EAGAIN&&errno!=EINTR)close_peer(w);else if(w->incoming.used&&now-w->peer_at>3000U)close_peer(w);return;}if(!w->incoming.used)w->peer_at=now;r=web_frame_feed(&w->incoming,buffer,(size_t)n);web_clear(buffer,sizeof(buffer));if(r<0){close_peer(w);return;}if(r==1){r=web_gateway_request(w,(char*)w->incoming.bytes+8,w->incoming.total-8,reply,sizeof(reply),now);if(r)web_frame_make(&w->outgoing,reply);web_clear(&w->incoming,sizeof(w->incoming));web_clear(reply,sizeof(reply));}
}
