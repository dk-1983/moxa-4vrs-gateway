#define _GNU_SOURCE
/* Reuse the minimal documented network fixture, not captured device files. */
#define main network_fixture_component_main
#include "test_network.c"
#undef main
#include "installer/install_orchestrator.h"
#include "installer/install_readiness.h"
#include "installer/install_scripts.h"
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
static unsigned int fail_sync;
int __real_fsync(int);
int __wrap_fsync(int fd){if(fail_sync){fail_sync=0;errno=ENOSPC;return -1;}return __real_fsync(fd);}
typedef struct fake {unsigned int running,stops,starts,entries;int cf,fail_stop,fail_start,fail_verify,execute,fail_io;const char*cut,*edit_root;unsigned int cut_index,hits;} fake_t;
static int detect_fake(void*v){(void)v;return 0;}
static int cf_fake(void*v){return ((fake_t*)v)->cf;}
static int inspect_fake(void*v,unsigned int*r){*r=((fake_t*)v)->running;return 0;}
static int stop_fake(void*v){fake_t*f=v;f->stops++;if(f->fail_stop)return -1;f->running=0;if(f->fail_io){f->fail_io=0;fail_sync=1;}return 0;}
static int start_fake(void*v,unsigned int old){fake_t*f=v;f->starts++;if(!old&&f->fail_start)return -1;f->running=1;return 0;}
static int verify_fake(void*v,unsigned int old){fake_t*f=v;return !old&&(!f->running||f->fail_verify)?-1:0;}
static int entry_fake(void*v,unsigned int app,const char*action,unsigned int fallback){fake_t*f=v;f->entries++;
 if(f->execute){
  if(app){FILE*out=fopen("/events","a");if(!out)return -1;fprintf(out,"application %s\n",action);return fclose(out);}
  if(fallback)return 0;
  {pid_t child=fork();int status;if(child<0)return -1;if(!child){execl("/etc/4vrs-installer/networking","networking",action,(char*)0);_exit(120);}return waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;}
 }
 return 0;
}
static const install_platform_t platform={detect_fake,cf_fake,inspect_fake,stop_fake,start_fake,verify_fake,entry_fake,0};
static gateway_network_environment_t environment;
static int cut(void*v,const char*s,unsigned int i){fake_t*f=v;
 if(f->edit_root&&!strcmp(s,"gate"))put(f->edit_root,"etc/resolv.conf","# concurrent operator edit\nnameserver 10.20.0.1\n");
 if(f->cut&&!strcmp(f->cut,s)&&i==f->cut_index){f->hits++;_exit(77);}return 0;}
static void context_init(install_context_t*c,fake_t*f,const char*root){memset(c,0,sizeof(*c));c->root=root;c->platform=&platform;c->platform_context=f;c->network=&environment;c->boundary=cut;c->boundary_context=f;}
static void script(const char*root,const char*name,const char*text){install_file_t f={1,0755,0,0};f.data=(unsigned char*)text;f.size=strlen(text);CHECK(!install_file_publish(root,name,&f));}
static void fixture(const char*out,char root[1024],install_package_t*p){
 static const char*dirs[]={"etc","etc/network","etc/rc.d","etc/rc.d/init.d","etc/rc.d/rcS.d","etc/rc.d/rc3.d","etc/rc.d/rc0.d","etc/rc.d/rc6.d","var","var/hda","var/hda/4vrs","var/hda/4vrs/config"};
 unsigned int i;char path[1024],buf[GATEWAY_CONFIG_MAX_BYTES];size_t n;gateway_persistent_config_t cfg;install_file_t f;
 CHECK(snprintf(root,1024,"%s/orchestration-XXXXXX",out)>0);CHECK(mkdtemp(root)!=0);
 for(i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++)directory(root,dirs[i]);
 CHECK(snprintf(path,sizeof(path),"%s/etc/init.d",root)>0);CHECK(!symlink("rc.d/init.d",path));
 put(root,"etc/network/interfaces",document);put(root,"etc/resolv.conf",resolver);
 gateway_persistent_defaults(&cfg);strcpy(cfg.ports[0].bind_address,"10.20.2.7");cfg.settings.backlight_on=0;cfg.settings.ntp_enabled=1;strcpy(cfg.settings.ntp_server,"10.20.0.1");
 CHECK(gateway_config_encode(&cfg,buf,sizeof(buf),&n)==GATEWAY_CONFIG_OK);buf[n]=0;put(root,"var/hda/4vrs/config/gateway.conf",buf);
 script(root,"etc/rc.d/init.d/networking","#!/bin/sh\ncase $1 in\nstart)\n/sbin/ifup -a\n;;\nstop)\n/sbin/ifdown -a\n;;\nrestart)\n/sbin/ifdown -a\n/sbin/ifup -a\n;;\nesac\n");
 script(root,"etc/rc.d/init.d/ntpdate","#!/bin/sh\ncase $1 in\nstart)\n /sbin/ntpdate own-server\n ;;\nesac\n");
 script(root,"etc/rc.d/init.d/halt","#!/bin/sh\nhwclock --systohc\n/sbin/halt\n");
 f.kind=2;f.mode=0777;f.data=(unsigned char*)"../init.d/networking";f.size=strlen((char*)f.data);CHECK(!install_file_publish(root,"etc/rc.d/rcS.d/S40networking",&f));
 memset(p,0,sizeof(*p));
 for(i=0;i<4;i++){p->payload[i].kind=1;p->payload[i].mode=0755;p->payload[i].data=(unsigned char*)(i==0?"qualification installer":i==1?"qualification gateway":"#!/bin/sh\nexit 0\n");p->payload[i].size=strlen((char*)p->payload[i].data);}
}
static void equal_set(install_context_t*c,unsigned int old){unsigned int i;install_file_t f;
 for(i=0;i<c->plan.count;i++){CHECK(!install_file_read(c->root,c->plan.member[i].path,&f));CHECK(install_file_equal(&f,old?&c->plan.member[i].before:&c->plan.member[i].after));install_file_free(&f);}
}
static void run_crash(const char*out,const char*point,unsigned int index,unsigned int late){
 char root[1024];install_package_t p;install_context_t c;fake_t f;pid_t pid;int status,r;install_file_t before,after;
 fixture(out,root,&p);memset(&f,0,sizeof(f));f.cf=1;f.running=1;f.cut=point;f.cut_index=index;
 CHECK(!install_file_read(root,"var/hda/4vrs/config/gateway.conf",&before));
 pid=fork();CHECK(pid>=0);if(!pid){context_init(&c,&f,root);r=install_orchestrate(&c,&p);fprintf(stderr,"missed cut %s/%u result=%d stage=%s\n",point,index,r,c.stage);_exit(78);}
 CHECK(waitpid(pid,&status,0)==pid);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);
 f.cut=0;context_init(&c,&f,root);
 if(!strcmp(point,"bootstrap-executable")||!strcmp(point,"journal")){
  CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);equal_set(&c,0);
 }else{
  if(late){f.cf=0;CHECK(install_recover_entry(&c,1,"start")==INSTALL_WAIT_CF);CHECK(f.entries==0);
   if(install_transaction_status(&c.transaction)==INSTALL_WAIT_CF){install_file_t link,gate;
    CHECK(!install_file_read(root,"etc/rc.d/rc3.d/S90fourvrs-gateway",&link));CHECK(link.kind==2);install_file_free(&link);
    CHECK(!install_file_read(root,"etc/rc.d/init.d/4vrs-gateway",&gate));CHECK(gate.kind==1&&strstr((char*)gate.data,"--application-entry"));install_file_free(&gate);
   }
   f.cf=1;}
  CHECK(!install_recover_entry(&c,0,"start"));CHECK(f.entries==1);
  CHECK(install_transaction_status(&c.transaction)==INSTALL_ROLLED_BACK);
  CHECK(!install_transaction_load(&c.transaction,&c.plan));equal_set(&c,1);
 }
 CHECK(!install_file_read(root,"var/hda/4vrs/config/gateway.conf",&after));CHECK(install_file_equal(&before,&after));install_file_free(&before);install_file_free(&after);install_context_release(&c);
}
static void readiness_test(const char*out){
 char root[1024],path[1024],buf[GATEWAY_CONFIG_MAX_BYTES];install_package_t p;gateway_persistent_config_t cfg;size_t n;unsigned int i;int fd,uart,status;pid_t child;struct sockaddr_in address;socklen_t length=sizeof(address);
 fixture(out,root,&p);directory(root,"dev");CHECK(snprintf(path,sizeof(path),"%s/dev/ttyM0",root)>0);CHECK(!symlink("/dev/zero",path));
 uart=open("/dev/zero",O_RDONLY);CHECK(uart>=0);fd=socket(AF_INET,SOCK_STREAM,0);CHECK(fd>=0);memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);CHECK(!bind(fd,(struct sockaddr*)&address,sizeof(address)));CHECK(!listen(fd,1));CHECK(!getsockname(fd,(struct sockaddr*)&address,&length));
 gateway_persistent_defaults(&cfg);for(i=1;i<8;i++)cfg.ports[i].enabled=0;strcpy(cfg.ports[0].bind_address,"127.0.0.1");cfg.ports[0].endpoint_port=ntohs(address.sin_port);
 CHECK(gateway_config_encode(&cfg,buf,sizeof(buf),&n)==GATEWAY_CONFIG_OK);buf[n]=0;put(root,"var/hda/4vrs/config/gateway.conf",buf);
 CHECK(!install_readiness_at(root,getpid()));close(uart);CHECK(install_readiness_at(root,getpid())<0);uart=open("/dev/zero",O_RDONLY);CHECK(uart>=0);
 child=fork();CHECK(child>=0);if(!child){for(;;)pause();}close(fd);
 /* The matching listener still exists globally, but only the child owns it. */
 CHECK(install_readiness_at(root,getpid())<0);CHECK(!kill(child,SIGTERM));CHECK(waitpid(child,&status,0)==child);close(uart);
}
static void managed_r15_test(const char*out){
 char root[1024],work[1024];install_package_t p;install_plan_t network;install_context_t c;fake_t f;install_file_t before,after,marker={1,0600,0,0};unsigned int i;
 fixture(out,root,&p);directory(root,"etc/4vrs-network");directory(root,"var/hda/4vrs/bin");directory(root,"shadow");directory(root,"shadow/store");
 CHECK(snprintf(work,sizeof(work),"%s/shadow",root)>0);memset(&network,0,sizeof(network));CHECK(!install_network_plan(root,work,&environment,&network,&stage));
 for(i=0;i<network.count;i++){CHECK(!install_file_publish(root,network.member[i].path,&network.member[i].after));}
 install_plan_free(&network);
 CHECK(!install_file_read(root,"etc/rc.d/init.d/networking",&before));CHECK(!install_vendor_script(&before,&after));CHECK(!install_file_publish(root,"etc/4vrs-network/vendor-networking",&before));CHECK(!install_file_publish(root,"etc/4vrs-network/vendor-networking-managed",&after));install_file_free(&before);install_file_free(&after);
 script(root,"etc/rc.d/init.d/ntpdate","#! /bin/sh\ntest -f /etc/4vrs-clock-managed && exit 0\ncase $1 in\nstart)\n /usr/sbin/ntpdate own-server\n;;\nesac\n");
 script(root,"etc/rc.d/init.d/halt","#! /bin/sh\nif ! test -f /etc/4vrs-clock-managed; then\n    hwclock --systohc\nfi\nhalt -d -f -i -p\n");
 marker.mode=0644;marker.data=(unsigned char*)"r15 fixture revision\n";marker.size=strlen((char*)marker.data);
 CHECK(!install_file_publish(root,"etc/4vrs-clock-managed",&marker));CHECK(!install_file_publish(root,"etc/4vrs-network/enabled",&marker));
 CHECK(!install_file_publish(root,"var/hda/4vrs/bin/4vrs-gateway",&p.payload[1]));CHECK(!install_file_publish(root,"etc/4vrs-network/gateway-network-recovery",&p.payload[1]));
 CHECK(!install_file_publish(root,"etc/rc.d/init.d/4vrs-gateway",&p.payload[2]));CHECK(!install_file_publish(root,"etc/rc.d/rcS.d/S40networking",&p.payload[3]));
 /* Enrolled managed profile, clock guards and legacy wrappers, but NO
  * installer state/journal/gates: the important r15 upgrade boundary. */
 p.payload[1].data=(unsigned char*)"managed upgrade gateway";p.payload[1].size=strlen((char*)p.payload[1].data);
 memset(&f,0,sizeof(f));f.cf=1;f.running=1;context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);equal_set(&c,0);
 for(i=0;i<5;i++){CHECK(install_file_equal(&c.plan.member[i].before,&c.plan.member[i].after));}
 install_context_release(&c);
}
static void clock_rejection_test(const char*out){
 unsigned int i;for(i=0;i<5;i++){char root[1024],path[1024];install_package_t p;install_context_t c;fake_t f;
  fixture(out,root,&p);memset(&f,0,sizeof(f));f.cf=f.running=1;
  if(i==0){CHECK(snprintf(path,sizeof(path),"%s/etc/4vrs-clock-managed",root)>0);CHECK(!symlink("resolv.conf",path));}
  if(i==1)directory(root,"etc/4vrs-clock-managed");
  if(i>=2)script(root,i==2?"etc/rc.d/init.d/ntpdate":i==3?"etc/rc.d/init.d/ntpdate.d":"etc/rc.d/init.d/halt","#!/bin/sh\n# /etc/4vrs-clock-managed\nexit 0\n");
  context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_REFUSED);CHECK(f.stops==0&&f.starts==0);
  CHECK(!strcmp(c.stage,i<2?"plan-clock-marker":i==2?"plan-clock-ntpdate":i==3?"plan-clock-ntpdate.d":"plan-clock-halt"));CHECK(c.detail>0);install_context_release(&c);
 }
}
#ifndef INSTALL_ORCHESTRATOR_MAIN
#define INSTALL_ORCHESTRATOR_MAIN main
#endif
int INSTALL_ORCHESTRATOR_MAIN(int argc,char**argv){
 char root[1024];install_context_t c;install_package_t p;fake_t f;unsigned int i;int r;
 memset(&environment,0,sizeof(environment));environment.read_network=observe;environment.read_lan2=lan;
 if(argc==3&&(!strcmp(argv[1],"--network-entry")||!strcmp(argv[1],"--application-entry"))){
  memset(&f,0,sizeof(f));f.cf=access("/missing-cf",F_OK)?1:0;f.execute=1;context_init(&c,&f,"/");
  r=install_recover_entry(&c,!strcmp(argv[1],"--application-entry"),argv[2]);
  return r==0?0:1;
 }
 CHECK(argc==2);
 readiness_test(argv[1]);
 managed_r15_test(argv[1]);
 clock_rejection_test(argv[1]);
 fixture(argv[1],root,&p);memset(&f,0,sizeof(f));f.cf=1;
 context_init(&c,&f,root);r=install_orchestrate(&c,&p);stage=c.stage;CHECK(r==INSTALL_COMPLETED);equal_set(&c,0);CHECK(f.starts==1&&f.stops==1);install_context_release(&c);
 for(i=0;i<4;i++){context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_UNCHANGED);CHECK(f.starts==1&&f.stops==1);install_context_release(&c);}
 /* Repair a missing owned clock marker, preserving all user settings. */
 {install_file_t absent={0,0,0,0};CHECK(!install_file_publish(root,"etc/4vrs-clock-managed",&absent));}
 context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);equal_set(&c,0);install_context_release(&c);
 /* An activation failure rolls back the WHOLE set and old service. */
 p.payload[1].data=(unsigned char*)"updated gateway";p.payload[1].size=strlen((char*)p.payload[1].data);f.fail_start=1;
 context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_ROLLED_BACK);CHECK(c.failure_stage&&!strcmp(c.failure_stage,"services-start"));equal_set(&c,1);CHECK(f.running);install_context_release(&c);
 run_crash(argv[1],"bootstrap-executable",0,0);run_crash(argv[1],"journal",0,0);run_crash(argv[1],"active",0,0);
 /* Actual first gate is member 15: use a discoverable index, not a timing race. */
 fixture(argv[1],root,&p);memset(&f,0,sizeof(f));f.cf=1;context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);
 for(i=0;i<c.plan.count;i++)if(c.plan.member[i].compact_flash&INSTALL_BOOT_GATE)run_crash(argv[1],"bootstrap-gate",i,1);
 for(i=0;i<c.plan.count;i++)if(!install_file_equal(&c.plan.member[i].before,&c.plan.member[i].after))run_crash(argv[1],"publish",i,1);
 install_context_release(&c);run_crash(argv[1],"stopped",0,0);run_crash(argv[1],"activated",0,1);run_crash(argv[1],"verified",0,0);
 for(i=0;i<6;i++){
  fixture(argv[1],root,&p);memset(&f,0,sizeof(f));f.cf=1;f.running=1;
  if(i==0)f.fail_stop=1;
  if(i==1)f.fail_verify=1;
  if(i==2)f.cf=0;
  if(i==3)script(root,"etc/rc.d/init.d/networking","#!/bin/sh\nunknown-hook\n");
  if(i==4)f.fail_io=1;
  if(i==5){char alias[1024];CHECK(snprintf(alias,sizeof(alias),"%s/etc/init.d",root)>0);CHECK(!unlink(alias));CHECK(!symlink("../outside",alias));}
  context_init(&c,&f,root);r=install_orchestrate(&c,&p);
  if(i==0){CHECK(r==INSTALL_RECOVERY_REQUIRED);f.fail_stop=0;CHECK(!install_recover_entry(&c,0,"start"));equal_set(&c,1);}
  if(i==1){CHECK(r==INSTALL_ROLLED_BACK);equal_set(&c,1);}
  if(i==2||i==3||i==5){CHECK(r==INSTALL_REFUSED);CHECK(!f.stops&&!f.starts);}
  if(i==4){CHECK(r==INSTALL_ROLLED_BACK);equal_set(&c,1);CHECK(!fail_sync);}
  install_context_release(&c);
 }
 /* Corruption is fail-closed even when a completed marker exists. */
 fixture(argv[1],root,&p);memset(&f,0,sizeof(f));f.cf=1;context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);
 {install_file_t j;CHECK(!install_file_read(c.journal_directory,"journal",&j));j.data[20]^=1;CHECK(!install_file_publish(c.journal_directory,"journal",&j));install_file_free(&j);}
 CHECK(install_recover_entry(&c,0,"start")==INSTALL_RECOVERY_REQUIRED);CHECK(f.entries==0);CHECK(install_recover_entry(&c,1,"start")==INSTALL_RECOVERY_REQUIRED);CHECK(f.entries==0);install_context_release(&c);
 fixture(argv[1],root,&p);memset(&f,0,sizeof(f));f.cf=1;f.running=1;f.edit_root=root;context_init(&c,&f,root);
 CHECK(install_orchestrate(&c,&p)==INSTALL_ROLLED_BACK);
 {install_file_t edited;CHECK(!install_file_read(root,"etc/resolv.conf",&edited));CHECK(!strcmp((char*)edited.data,"# concurrent operator edit\nnameserver 10.20.0.1\n"));install_file_free(&edited);}
 install_context_release(&c);
 /* Recovery itself can be interrupted; the next invocation remains safe. */
 fixture(argv[1],root,&p);memset(&f,0,sizeof(f));f.cf=1;f.running=1;f.cut="publish";f.cut_index=0;
 {pid_t child;int status;child=fork();CHECK(child>=0);if(!child){context_init(&c,&f,root);(void)install_orchestrate(&c,&p);_exit(78);}CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);
  f.cut="restore";child=fork();CHECK(child>=0);if(!child){context_init(&c,&f,root);(void)install_recover_entry(&c,0,"start");_exit(78);}CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);
 }
 f.cut=0;context_init(&c,&f,root);CHECK(install_recover_only(&c)==INSTALL_ROLLED_BACK);CHECK(f.entries==0);CHECK(!install_transaction_load(&c.transaction,&c.plan));equal_set(&c,1);install_context_release(&c);
 /* Leave a terminal fixture for the actual shell-gate/chroot test. */
 fixture(argv[1],root,&p);memset(&f,0,sizeof(f));f.cf=1;context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);install_context_release(&c);
 {char location[1024];FILE*out;CHECK(snprintf(location,sizeof(location),"%s/shell-root.txt",argv[1])>0);out=fopen(location,"w");CHECK(out!=0);CHECK(fprintf(out,"%s\n",root)>0);CHECK(!fclose(out));}
 printf("installer production orchestration: %u checks passed (fake hardware only)\n",checks);return 0;
}
