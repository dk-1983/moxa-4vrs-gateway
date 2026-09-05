#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "network/gateway_network_runtime.h"
#include "network/gateway_network_store.h"

/* Separate qualification binary. No production providers or live paths used. */
typedef struct shared {
 gateway_lan_observation_t live;
 volatile int hold,point,pid,after_unlink,writes,late;
} shared_t;
static shared_t *shared;
static char store[256],iface[300],resolver[300],trace[300],self[512];
static unsigned long now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))exit(90);return (unsigned long)t.tv_sec*1000UL+(unsigned long)t.tv_nsec/1000000UL;}
static int failure;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%u predicate=%s\n",(unsigned int)__LINE__,#x);failure=1;goto done;}}while(0)
static void event(const char *name,int value){printf("ms=%lu event=%s value=%d\n",now(),name,value);fflush(stdout);}
static void barrier(int point){unsigned long end=now()+10000UL;shared->pid=(int)getpid();shared->point=point;while(now()<end)usleep(10000);_exit(72);}
int __real_unlink(const char *);
int __real_fsync(int);
int __wrap_unlink(const char *p){char expected[300];int r;snprintf(expected,sizeof(expected),"%s/commit.guard",store);
 if(shared&&shared->hold&&!strcmp(p,expected)){
  if(shared->hold==2)barrier(2);
  r=__real_unlink(p);if(!r)shared->after_unlink=1;return r;
 }return __real_unlink(p);}
int __wrap_fsync(int fd){int r=__real_fsync(fd);struct stat s;
 if(!r&&shared&&shared->hold==3&&shared->after_unlink&&!fstat(fd,&s)&&S_ISDIR(s.st_mode))barrier(3);
 return r;}
static int read_lan(void *c,gateway_lan_observation_t *out){(void)c;*out=shared->live;return 0;}
static int write_lan(void *c,const gateway_lan_observation_t *in){(void)c;
 ++shared->writes;shared->live=*in;
 if(shared->hold==1&&!strcmp(in->address,"192.168.4.126")){barrier(1);++shared->late;}
 return 0;}
static int file(const char *p,const char *text){FILE*f=fopen(p,"wx");int r;if(!f)return -1;r=fputs(text,f)<0;if(fclose(f))r=1;return r?-1:0;}
static int identity(int pid,char *exe,size_t cap,unsigned long *start){char path[80],text[2048],*p;FILE*f;int i;ssize_t n;
 snprintf(path,sizeof(path),"/proc/%d/exe",pid);n=readlink(path,exe,cap-1);if(n<0)return -1;exe[n]=0;
 snprintf(path,sizeof(path),"/proc/%d/stat",pid);f=fopen(path,"r");if(!f)return -1;
 if(!fgets(text,sizeof(text),f)){fclose(f);return -1;}fclose(f);p=strrchr(text,')');if(!p)return -1;p+=2;
 for(i=3;i<22;++i){p=strchr(p,' ');if(!p)return -1;++p;}*start=strtoul(p,0,10);return 0;}
static int checked_kill(int pid,unsigned long expected){char exe[512];unsigned long start;
 if(pid<=1||identity(pid,exe,sizeof(exe),&start)||strcmp(exe,self)||start!=expected)return -1;
 printf("signal pid=%d exe=%s starttime=%lu signal=9\n",pid,exe,start);fflush(stdout);return kill(pid,SIGKILL);}
static int wait_point(int point,unsigned long limit){unsigned long end=now()+limit;while(now()<end){if(shared->point==point)return 0;usleep(1000);}return -1;}
static int wait_child(int pid,int *status,unsigned long limit){unsigned long end=now()+limit;while(now()<end){int r=(int)waitpid(pid,status,WNOHANG);if(r==pid)return 0;if(r<0)return -1;usleep(1000);}return -1;}
static int valid_path(const char *p){const char *tail;char parent[256];struct stat st;size_t i,n;
 if(!strncmp(p,"/tmp/4vrs-qualification-",24))tail=p+24;
 else if(!strncmp(p,"/var/hda/4vrs/tests/qualification-",34))tail=p+34;
 else return 0;
 if(!*tail||strlen(p)>230)return 0;
 for(i=0;tail[i];++i)if(!((tail[i]>='a'&&tail[i]<='z')||(tail[i]>='0'&&tail[i]<='9')||tail[i]=='-'))return 0;
 /* Reject symlink ancestors, including a redirected CF test directory. */
 n=strlen(p);for(i=1;i<n;++i)if(p[i]=='/'){memcpy(parent,p,i);parent[i]=0;if(lstat(parent,&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return 0;}
 return lstat(p,&st)<0&&errno==ENOENT;}
int main(int argc,char **argv){gateway_network_environment_t e;gateway_network_runtime_t runtime;
 gateway_network_settings_t settings;gateway_network_profile_t candidate;char old[32768],next[32768],got[32768],exe[512];size_t on=0,nn=0,n=0;
 const char *bindings[8]={"0.0.0.0","0.0.0.0","0.0.0.0","0.0.0.0","0.0.0.0","0.0.0.0","0.0.0.0","0.0.0.0"};
 int child=0,status=0,lock=-1,mode,negative=0,acted=0,initialized=0;unsigned long start=0,end;const char *stage=0;
 memset(&runtime,0,sizeof(runtime));runtime.fd=-1;
 if(argc!=5||!valid_path(argv[1])||(strcmp(argv[2],"host")&&strcmp(argv[2],"moxa1")&&strcmp(argv[2],"moxa2"))){fprintf(stderr,"REFUSE arguments/path/target (new qualification directory required)\n");return 2;}
 mode=!strcmp(argv[3],"apply-peer")?1:!strcmp(argv[3],"keep-before")?2:!strcmp(argv[3],"keep-after")?3:!strcmp(argv[3],"writer-timeout")?4:0;
 if(!mode)return 2;
 if(strcmp(argv[4],"normal")){negative=!strcmp(argv[4],"wrong-identity")?1:!strcmp(argv[4],"no-effect")?2:!strcmp(argv[4],"no-boundary")?3:0;if(!negative)return 2;}
 if(identity((int)getpid(),self,sizeof(self),&start))return 2;
 printf("qualification=v1 target_label=%s case=%s injection=%s exe=%s parent_start=%lu\n",argv[2],argv[3],argv[4],self,start);fflush(stdout);
 strcpy(store,argv[1]);if(mkdir(store,0700))return 2;
 snprintf(iface,sizeof(iface),"%s/interfaces",store);snprintf(resolver,sizeof(resolver),"%s/resolver",store);snprintf(trace,sizeof(trace),"%s/trace",store);
 shared=mmap(0,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);if(shared==MAP_FAILED)return 2;memset(shared,0,sizeof(*shared));
 CHECK(!file(iface,"auto eth0 eth1\niface eth0 inet static\n address 10.0.2.13\n netmask 255.255.240.0\n gateway 10.0.0.1\niface eth1 inet static\n address 192.168.4.127\n netmask 255.255.255.0\n broadcast 192.168.4.255\n"));
 CHECK(!file(resolver,"nameserver 10.0.0.1\n"));
 shared->live.present=shared->live.up=1;strcpy(shared->live.address,"192.168.4.127");strcpy(shared->live.netmask,"255.255.255.0");strcpy(shared->live.broadcast,"192.168.4.255");
 memset(&e,0,sizeof(e));e.store_directory=store;e.interfaces_path=iface;e.resolver_path=resolver;e.trace_path=trace;e.read_lan2=read_lan;e.write_lan2=write_lan;
 CHECK(!gateway_network_enroll_detailed(&e,bindings,&stage));CHECK(!gateway_network_runtime_init(&runtime,&e));initialized=1;
 CHECK(!gateway_network_store_boot(store,old,sizeof(old),&on));settings=runtime.confirmed.settings;strcpy(settings.lan[1].address,"192.168.4.126");
 CHECK(!gateway_network_profile_candidate(&runtime.confirmed,&settings,&candidate));CHECK(!gateway_network_profile_encode(&candidate,next,sizeof(next),&nn));
 if(mode==2||mode==3){
  CHECK(!gateway_network_store_write(store,"candidate",next,nn));shared->hold=negative==3?0:mode;
  child=(int)fork();CHECK(child>=0);if(!child){int l=gateway_network_store_lock(store);if(l<0)_exit(73);_exit(gateway_network_store_confirm(store)?74:0);}
  CHECK(!identity(child,exe,sizeof(exe),&start)&&!strcmp(exe,self));
  CHECK(!wait_point(mode,2000));event("boundary",shared->point);CHECK(shared->pid==child);
  if(negative==2){CHECK(0);}CHECK(!checked_kill(child,start+(negative==1?1UL:0UL)));acted=1;
  CHECK(!wait_child(child,&status,2000));child=0;CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL);
  shared->hold=0;lock=gateway_network_store_lock(store);CHECK(lock>=0);
  CHECK(!gateway_network_store_boot(store,got,sizeof(got),&n));
  CHECK(mode==2?(n==on&&!memcmp(got,old,n)):(n==nn&&!memcmp(got,next,n)));
  CHECK(!gateway_network_store_abort_commit(store));CHECK(!gateway_network_store_discard(store));
 }else{
  shared->hold=mode==1&&negative!=3?1:0;CHECK(!gateway_network_runtime_start(&runtime,&settings));
  end=now()+5000;while(now()<end){gateway_network_runtime_poll(&runtime);if(mode==1&&shared->point==1)break;
   if(mode==4&&runtime.status.state==GATEWAY_NETWORK_WAIT_BINDINGS)CHECK(!gateway_network_runtime_command(&runtime,'B'));
   if(mode==4&&runtime.status.state==GATEWAY_NETWORK_WAIT_CONFIRM)break;
   usleep(1000);}
  if(mode==1){CHECK(shared->point==1&&runtime.status.state==GATEWAY_NETWORK_APPLYING);event("APPLYING-held",shared->pid);
   CHECK(!identity(shared->pid,exe,sizeof(exe),&start)&&!strcmp(exe,self));
   if(negative==1||negative==2)CHECK(0);
   gateway_network_runtime_disconnect(&runtime);acted=1;
  }else{CHECK(runtime.status.state==GATEWAY_NETWORK_WAIT_CONFIRM);shared->hold=negative==3?0:2;
   CHECK(!gateway_network_runtime_command(&runtime,'K'));CHECK(!wait_point(2,2000));
   CHECK(!identity(shared->pid,exe,sizeof(exe),&start)&&!strcmp(exe,self));event("COMMIT-writer-held",shared->pid);shared->hold=0;acted=1;}
  end=now()+7000;while(runtime.pid&&now()<end){gateway_network_runtime_poll(&runtime);if(runtime.status.state==GATEWAY_NETWORK_ROLLBACK_BINDINGS)gateway_network_runtime_command(&runtime,'B');usleep(1000);}
  CHECK(!runtime.pid);shared->hold=0;CHECK(!kill(shared->pid,0)?0:errno==ESRCH);CHECK(!shared->late);
  CHECK(!strcmp(shared->live.address,"192.168.4.127"));CHECK(!gateway_network_store_boot(store,got,sizeof(got),&n));CHECK(n==on&&!memcmp(old,got,n));
  {FILE*f=fopen(trace,"r");char lines[12000];size_t count;CHECK(f!=0);count=fread(lines,1,sizeof(lines)-1,f);fclose(f);lines[count]=0;
   CHECK(strstr(lines,mode==1?"state=9 reason=1":"state=9 reason=3")!=0);
   CHECK(strstr(lines,"event=8")!=0);}
  /* A disconnected client cannot receive terminal; trace is authoritative. */
  if(mode==4)CHECK(runtime.status.state==GATEWAY_NETWORK_REVERTED);
  event("supervisor-REVERTED-trace",9);
 }
 CHECK(acted);end=now()+300;while(now()<end)usleep(1000);
 CHECK(!gateway_network_store_boot(store,got,sizeof(got),&n));CHECK(mode==3?(n==nn&&!memcmp(got,next,n)):(n==on&&!memcmp(got,old,n)));
 CHECK(!gateway_network_boot_restore(&e));event(mode==3?"PASS-new-authorized-generation":"PASS-old-generation",shared->writes);
done:
 shared->hold=0;
 if(child>0){if(!checked_kill(child,start))wait_child(child,&status,2000);else wait_child(child,&status,11000);}
 if(initialized&&runtime.pid){gateway_network_runtime_disconnect(&runtime);end=now()+12000;while(runtime.pid&&now()<end){gateway_network_runtime_poll(&runtime);usleep(1000);}if(runtime.pid)failure=1;}
 if(lock>=0)gateway_network_store_unlock(lock);
 printf("result=%s acted=%d retained_store=%s\n",failure?"FAIL":"PASS",acted,store);
 return failure?1:0;
}
