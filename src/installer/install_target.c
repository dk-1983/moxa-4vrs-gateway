#define _GNU_SOURCE
#include "installer/install_target.h"
#include "installer/install_readiness.h"
#include "network/gateway_network_boot.h"
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define APP "/var/hda/4vrs/bin/4vrs-gateway"
#define HELPER "/etc/4vrs-network/gateway-network-recovery"
static unsigned long long tick(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(unsigned long long)t.tv_sec*1000U+(unsigned long long)t.tv_nsec/1000000U;}
static int cf_available(void*v){struct stat a,b;struct statvfs fs;(void)v;if(stat("/var",&a)||stat("/var/hda",&b))return errno==ENOENT?0:-1;if(a.st_dev==b.st_dev)return 0;if(!S_ISDIR(b.st_mode)||statvfs("/var/hda",&fs))return -1;return(fs.f_flag&ST_RDONLY)?-1:1;}
static int detect(void*v){struct utsname u;struct stat s;unsigned int i;char path[64];struct statvfs fs;(void)v;
 if(geteuid()!=0||uname(&u)||strncmp(u.machine,"arm",3)||strncmp(u.release,"2.6.10",6)||!strstr(u.release,"xscale_be"))return -1;
 for(i=0;i<8;i++){snprintf(path,sizeof(path),"/dev/ttyM%u",i);if(stat(path,&s)||!S_ISCHR(s.st_mode))return -1;}
 if(stat("/usr/lib/libmoxalib.so",&s)||!S_ISREG(s.st_mode)||statvfs("/etc",&fs)||(fs.f_flag&ST_RDONLY))return -1;
 return 0;
}
static int service(gateway_network_service_status_t*s){return gateway_network_service_query(gateway_network_service_production(),s);}
static int guardian_pid(pid_t*pid){int fd;struct stat s;struct flock lock;fd=open("/etc/4vrs-network/owner.lock",O_RDWR|O_NOFOLLOW);if(fd<0)return errno==ENOENT?0:-1;if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_nlink!=1||s.st_uid!=geteuid()){close(fd);return -1;}memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;if(fcntl(fd,F_GETLK,&lock)){close(fd);return -1;}close(fd);if(lock.l_type==F_UNLCK)return 0;if(lock.l_pid<=1)return -1;*pid=lock.l_pid;return 1;}
static int identity(pid_t pid,install_process_t*p){return !install_process_capture(pid,HELPER,p)||!install_process_capture(pid,APP,p)?0:-1;}
static int parent(pid_t pid,pid_t*ppid){char path[64],b[2048],*p;int fd;ssize_t n;long number;snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);fd=open(path,O_RDONLY);if(fd<0)return -1;n=read(fd,b,sizeof(b)-1);close(fd);if(n<=0||n==(ssize_t)sizeof(b)-1)return -1;b[n]=0;p=strrchr(b,')');if(!p||sscanf(p+1," %*c %ld",&number)!=1||number<0)return -1;*ppid=(pid_t)number;return 0;}
static int scan(void*v,unsigned int*running,unsigned int readiness){
 install_target_t*t=v;gateway_network_service_status_t s;DIR*d;struct dirent*e;unsigned int count=0,seen=0;struct stat exe,helper;int have_exe=!stat(APP,&exe),have_helper=!stat(HELPER,&helper);
 t->have_application=t->have_guardian=0;memset(&t->owner,0,sizeof(t->owner));
 {pid_t g;int held=guardian_pid(&g);if(held<0)return -1;if(held){if(identity(g,&t->guardian))return -1;t->have_guardian=1;}}
 if(!service(&s)){
  pid_t pp;if((readiness&&(!s.ready||!s.settled||s.error))||!t->have_guardian||s.guardian_pid!=t->guardian.pid||identity((pid_t)s.owner_pid,&t->owner)||parent((pid_t)s.owner_pid,&pp)||pp!=(pid_t)s.guardian_pid)return -1;
 }else if(readiness&&t->have_guardian)return -1;
 d=opendir("/proc");if(!d)return -1;
 for(;;){char*end;long pid;char path[64],cmd[256];struct stat st;pid_t pp;int fd;ssize_t n;
  errno=0;e=readdir(d);if(!e){if(errno){closedir(d);return -1;}break;}
  pid=strtol(e->d_name,&end,10);if(*end||pid<=1)continue;if(++seen>4096){closedir(d);return -1;}
  snprintf(path,sizeof(path),"/proc/%ld/cmdline",pid);fd=open(path,O_RDONLY);if(fd>=0){n=read(fd,cmd,sizeof(cmd)-1);close(fd);if(n>0){cmd[n]=0;if(strstr(cmd,"mbusd")||strstr(cmd,"dhcpcd")){closedir(d);return -1;}
    if(strstr(cmd,"ntpdate")||strstr(cmd,"ntpd")){install_process_t ancestor;pid_t pp;if(parent((pid_t)pid,&pp)||install_process_capture(pp,APP,&ancestor)){closedir(d);return -1;}}}}
  snprintf(path,sizeof(path),"/proc/%ld/exe",pid);if(stat(path,&st))continue;
  if(!(have_exe&&st.st_dev==exe.st_dev&&st.st_ino==exe.st_ino)&&!(have_helper&&st.st_dev==helper.st_dev&&st.st_ino==helper.st_ino))continue;
  if(t->have_guardian&&(pid==t->guardian.pid||pid==t->owner.pid))continue;
  if(!parent((pid_t)pid,&pp)){install_process_t ancestor;if(t->have_guardian&&(pp==t->guardian.pid||pp==t->owner.pid))continue;if(pp>1&&!install_process_capture(pp,APP,&ancestor))continue;}
  if(++count>1||install_process_capture((pid_t)pid,APP,&t->application)){closedir(d);return -1;}
  t->have_application=1;
 }
 closedir(d);*running=t->have_application;
 if(readiness&&!t->baseline_valid){if(gateway_network_observe(&t->baseline)||t->baseline.unsupported)return -1;t->baseline_valid=1;}
 return 0;
}
static int inspect(void*v,unsigned int*running){
 install_target_t*t=v;
 if(!access("/var/hda/4vrs/disable-autostart",F_OK)){t->installer->stage="user-disabled-autostart";return -1;}
 if(scan(v,running,0))return -1;
 if(!t->baseline_valid){if(gateway_network_observe(&t->baseline)||t->baseline.unsupported)return -1;t->baseline_valid=1;}
 return 0;
}
static int descendants(const install_target_t*t,install_process_t children[128],unsigned int*count){
 unsigned int pass;*count=0;
 for(pass=0;pass<4;pass++){
  DIR*d=opendir("/proc");struct dirent*e;unsigned int before=*count,seen=0;
  if(!d)return -1;
  for(;;){char*end,path[64];long pid;pid_t pp;unsigned int i,ours=0;
   errno=0;e=readdir(d);if(!e){if(errno){closedir(d);return -1;}break;}
   pid=strtol(e->d_name,&end,10);if(*end||pid<=1)continue;
   if(++seen>4096){closedir(d);return -1;}
   if(parent((pid_t)pid,&pp))continue;
   if((t->have_application&&pp==t->application.pid)||(t->have_guardian&&pp==t->guardian.pid))ours=1;
   for(i=0;i<*count;i++){if(children[i].pid==pid){ours=0;break;}if(children[i].pid==pp)ours=1;}
   if(!ours)continue;
   if(*count>=128){closedir(d);return -1;}
   snprintf(path,sizeof(path),"/proc/%ld/exe",pid);
   if(install_process_capture((pid_t)pid,path,&children[*count])){closedir(d);return -1;}
   (*count)++;
  }
  closedir(d);if(before==*count)return 0;
 }
 return -1; /* Unknown deeper tree is not silently truncated. */
}
static int stop(void*v){
 install_target_t*t=v;unsigned int running;install_file_t absent={0,0,0,0};int status;pid_t g;
 install_process_t children[128];unsigned int count,i;
 /* Refresh identities at every stop, including rollback after new activation. */
 if(scan(v,&running,0))return -1;
 if(descendants(t,children,&count))return -1;
 if(t->have_application){if(install_process_stop(&t->application,55000))return -1;(void)waitpid(t->application.pid,&status,WNOHANG);t->have_application=0;}
 {int held=guardian_pid(&g);if(held<0)return -1;if(held){install_process_t guardian;if(identity(g,&guardian)||install_process_stop(&guardian,35000))return -1;(void)waitpid(guardian.pid,&status,WNOHANG);}}
 t->have_guardian=0;
 if(guardian_pid(&g)!=0)return -1;
 for(i=0;i<count;i++)if(install_process_matches(&children[i])!=0)return -1;
 if(scan(v,&running,0)||running||t->have_guardian)return -1;
 if(cf_available(v)!=1)return 0;
 return install_file_publish("/","var/hda/4vrs/run/4vrs-gateway.pid",&absent);
}
static int command(install_target_t*t,const char*file,const char*arg,unsigned int timeout,const char*stage){char*argv[3];argv[0]=(char*)file;argv[1]=(char*)arg;argv[2]=0;t->installer->stage=stage;t->installer->detail=install_process_run(file,argv,timeout);return t->installer->detail;}
static int start_application(void*v){
 install_target_t*t=v;int status,fd;pid_t child,p;unsigned long long began=tick();char number[32];install_file_t pidfile;
 t->installer->stage="application-resources";
 if(!began)return -1;
 child=fork();if(child<0)return -1;
 if(!child){
  if(gateway_network_process_isolate(-1))_exit(120);
  fd=open("/dev/console",O_RDWR|O_NOCTTY);
  if(fd<0||dup2(fd,0)<0||dup2(fd,1)<0||dup2(fd,2)<0)_exit(121);
  if(fd>2)close(fd);
  execl(APP,APP,(char*)0);_exit(123);
 }
 while(tick()-began<55000U){
  p=waitpid(child,&status,WNOHANG);
  if(p==child)return -1;
  if(p<0&&errno!=EINTR)break;
  if(!install_process_capture(child,APP,&t->application)&&!install_readiness(child)){
   t->have_application=1;
   snprintf(number,sizeof(number),"%ld\n",(long)child);
   pidfile.kind=1;pidfile.mode=0600;pidfile.size=strlen(number);pidfile.data=(unsigned char*)number;
   return install_file_publish("/","var/hda/4vrs/run/4vrs-gateway.pid",&pidfile);
  }
  usleep(100000);
 }
 /* Unreaped direct child reserves its PID and private process group. */
 (void)kill(child,SIGKILL);(void)kill(-child,SIGKILL);began=tick();
 do{p=waitpid(child,&status,WNOHANG);if(p==child)break;usleep(10000);}while(tick()-began<2000U);
 return -1;
}
static int network_start(install_target_t*t,unsigned int runtime){
 unsigned long long began;gateway_network_service_status_t status;
 if(access("/etc/4vrs-network/enabled",F_OK))return command(t,"/etc/init.d/networking","start",30000,"vendor-original-start");
 if(command(t,HELPER,"--network-boot",10000,"network-boot")||command(t,"/etc/4vrs-network/vendor-networking-managed",runtime?"restart":"start",30000,"vendor-phase")||command(t,HELPER,"--network-owner-start",35000,"owner-start"))return -1;
 began=tick();if(!began)return -1;
 do{if(!service(&status)&&status.ready&&status.settled&&!status.error)return 0;usleep(100000);}while(tick()-began<35000U);
 return -1;
}
static int start(void*v,unsigned int old){
 /* Restoring a formerly unmanaged running installation does not repeat
  * vendor ifup on its already configured interfaces. Guardian termination
  * restores the imported baseline; verify checks it before completion. */
 if((!old||!access("/etc/4vrs-network/enabled",F_OK))&&network_start(v,1))return -1;
 return start_application(v);
}
static int verify(void*v,unsigned int old){
 install_target_t*t=v;gateway_network_observation_t now;gateway_network_service_status_t s;unsigned int i,managed=!access("/etc/4vrs-network/enabled",F_OK);
 if(t->have_application){if(install_process_matches(&t->application)!=1||install_readiness(t->application.pid))return -1;}
 else if(!old)return -1;
 if(gateway_network_observe(&now)||now.unsupported)return -1;
 if(managed&&(service(&s)||!s.ready||!s.settled||s.error))return -1;
 if(t->baseline_valid){for(i=0;i<2;i++){
   if(managed&&s.policy.lan[i].mode==GATEWAY_LAN_DHCP_CLIENT){if(!s.lease_valid[i])return -1;continue;}
   if(strcmp(now.lan[i].address,t->baseline.lan[i].address)||strcmp(now.lan[i].netmask,t->baseline.lan[i].netmask)||strcmp(now.lan[i].broadcast,t->baseline.lan[i].broadcast)||now.lan[i].up!=t->baseline.lan[i].up)return -1;
  }
  if(now.default_lan!=t->baseline.default_lan)return -1;
  if((!managed||!s.policy.default_lan||s.policy.lan[s.policy.default_lan-1].mode==GATEWAY_LAN_STATIC)&&strcmp(now.gateway,t->baseline.gateway))return -1;
  if((!managed||!s.policy.automatic_dns)&&memcmp(now.dns,t->baseline.dns,sizeof(now.dns)))return -1;
 }
 return 0;
}
static int entry(void*v,unsigned int application,const char*action,unsigned int fallback){
 install_target_t*t=v;char path[128];
 if(!strcmp(action,"start")&&fallback)return network_start(t,0);
 if(application){
  if(!strcmp(action,"start")){unsigned int running;if(!access("/var/hda/4vrs/disable-autostart",F_OK))return 0;if(inspect(v,&running))return -1;if(running)return 0;return start_application(v);}
  if(!strcmp(action,"stop"))return stop(v);
  if(!strcmp(action,"status")){unsigned int running;return inspect(v,&running)?1:running?0:3;}
  return -1;
 }
 snprintf(path,sizeof(path),"/%s/networking",INSTALL_RECOVERY_DIRECTORY);
 if(access(path,X_OK))return network_start(t,0);
 (void)t;return command(t,path,action,45000,"network-entry");
}
static int capacity(void*v,size_t early,size_t compact){
 struct statvfs fs;(void)v;
 if(statvfs("/etc",&fs)||(unsigned long long)fs.f_bavail*fs.f_frsize<early)return -1;
 if(statvfs("/var/hda",&fs)||(unsigned long long)fs.f_bavail*fs.f_frsize<compact)return -1;
 return 0;
}
static const install_platform_t platform={detect,cf_available,inspect,stop,start,verify,entry,capacity};
void install_target_init(install_context_t*c,install_target_t*t){memset(c,0,sizeof(*c));memset(t,0,sizeof(*t));t->installer=c;c->root="/";c->platform=&platform;c->platform_context=t;c->network=gateway_network_environment_production();c->stage="initial";}
int install_target_lock(void){int fd;struct stat s;fd=open("/etc/4vrs-installer/install.lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(fd<0)return -1;if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_nlink!=1||s.st_uid!=0||(s.st_mode&0077)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;}
