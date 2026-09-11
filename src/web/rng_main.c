#define _GNU_SOURCE
#include "web/exec_fds.h"
#include "web/rng_nv.h"
#include "web/rng_wire.h"
#include "version.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void clear(void*p,size_t n){volatile unsigned char*b=p;while(n--)*b++=0;}
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static int prepare_stop(void){struct sigaction sa;sigset_t set;
 memset(&sa,0,sizeof(sa));sa.sa_handler=stop;sa.sa_flags=SA_RESTART;sigemptyset(&sa.sa_mask);
 if(sigaction(SIGTERM,&sa,NULL)||sigaction(SIGINT,&sa,NULL))return -1;
 sigemptyset(&set);sigaddset(&set,SIGTERM);sigaddset(&set,SIGINT);
 /* Gateway blocks these across fork/exec. Deliver pending stops only after
  * handlers exist; a handler records intent and never interrupts a commit. */
 return sigprocmask(SIG_UNBLOCK,&set,NULL);
}
static unsigned long now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (unsigned long)t.tv_sec;}
static int broker(void){
 struct channel {unsigned char rx[16],tx[1040];size_t used,sent,total;unsigned int seq;int attached;unsigned long at;} c[2];
 unsigned int epoch=(unsigned int)rng_nv_generation(),requests=0,rotations=0,i;unsigned long rotated=now(),interval=60;
#ifdef WEB_HOST_TEST
 if(getenv("NV_FAST_ROTATION"))interval=0;
#endif
 memset(c,0,sizeof(c));c[0].attached=c[1].attached=-1;
 for(i=0;i<2;i++)if(fcntl(3+(int)i,F_SETFL,O_NONBLOCK))return 20;
 for(;!stopping;){fd_set rd,wr;struct timeval t={0,100000};int r;FD_ZERO(&rd);FD_ZERO(&wr);
  for(i=0;i<2;i++){int queued=0;if(ioctl(3+(int)i,FIONREAD,&queued)||queued>8*16)return 26;if(c[i].total)FD_SET(3+i,&wr);else FD_SET(3+i,&rd);if((c[i].used||c[i].total)&&now()-c[i].at>3)return 21;}
  r=select(5,&rd,&wr,NULL,&t);if(stopping)return 0;if(r<0){if(errno==EINTR)continue;return 21;}
  for(i=0;i<2;i++){
   struct channel*p=&c[i];ssize_t n;int fd=3+(int)i;
   if(p->total&&FD_ISSET(fd,&wr)){n=send(fd,p->tx+p->sent,p->total-p->sent,MSG_NOSIGNAL);if(n>0)p->sent+=(size_t)n;else if(!n||(errno!=EINTR&&errno!=EAGAIN))return 22;if(p->sent==p->total){clear(p->tx,sizeof(p->tx));p->sent=p->total=0;p->used=0;}continue;}
   if(p->total||!FD_ISSET(fd,&rd))continue;
   if(!p->used)p->at=now();{struct msghdr m;struct iovec v;struct cmsghdr*h;union{struct cmsghdr align;unsigned char b[CMSG_SPACE(sizeof(int)*8)];} control;
    memset(&m,0,sizeof(m));v.iov_base=p->rx+p->used;v.iov_len=16-p->used;m.msg_iov=&v;m.msg_iovlen=1;m.msg_control=control.b;m.msg_controllen=sizeof(control.b);n=recvmsg(fd,&m,0);
    if(n>0){if(m.msg_flags&MSG_CTRUNC)return 23;for(h=CMSG_FIRSTHDR(&m);h;h=CMSG_NXTHDR(&m,h)){if(i||p->attached>=0||h->cmsg_level!=SOL_SOCKET||h->cmsg_type!=SCM_RIGHTS||h->cmsg_len!=CMSG_LEN(sizeof(int)))return 23;memcpy(&p->attached,CMSG_DATA(h),sizeof(int));}}}
   if(n>0)p->used+=(size_t)n;else if(!n||(errno!=EINTR&&errno!=EAGAIN))return 22;
   if(p->used==16){unsigned int len=rng_get(p->rx+8),seq=rng_get(p->rx+4);if(!seq||seq!=p->seq+1||rng_get(p->rx+12))return 23;
    if(!memcmp(p->rx,"RGA1",4)&&!i&&!len&&p->attached>=0){int type;socklen_t z=sizeof(type);if(getsockopt(p->attached,SOL_SOCKET,SO_TYPE,&type,&z)||type!=SOCK_STREAM)return 23;
     if(dup2(p->attached,4)<0)return 23;if(p->attached!=4)close(p->attached);p->attached=-1;if(fcntl(4,F_SETFL,O_NONBLOCK))return 23;
     clear(&c[1],sizeof(c[1]));c[1].attached=-1;memcpy(p->tx,"RNG1",4);rng_put(p->tx+4,seq);rng_put(p->tx+8,0);rng_put(p->tx+12,epoch);p->seq=seq;p->total=16;p->sent=0;clear(p->rx,16);continue;
    }
    if(memcmp(p->rx,"RNG1",4)||!len||len>1024||p->attached>=0)return 23;
    /* Trial policy is deliberately bounded: at most 4 rotations/process, at
     * least 60 seconds apart. Exhaustion is diagnostic failure, not auto reboot. */
    if(requests==1024){if((!rng_nv_production()&&rotations==4)||now()<rotated||now()-rotated<interval)return rng_nv_production()?8:24;
#ifdef WEB_HOST_TEST
     if(getenv("NV_ROTATE_FAIL"))setenv("NV_FAIL",getenv("NV_ROTATE_FAIL"),1);
#endif
     if(rng_nv_rotate())return rng_nv_production()?rng_nv_error():25;requests=0;rotations++;rotated=now();}
    if(stopping)return 0;
    memcpy(p->tx,p->rx,16);rng_put(p->tx+12,epoch);
    if(rng_nv_random(p->tx+16,len))return 25;
    requests++;
#ifdef WEB_HOST_TEST
    fprintf(stderr,"rng-allocation requests=%u bytes=%u rotations=%u\n",requests,len,rotations);
#endif
    p->seq=seq;p->total=16+len;p->sent=0;clear(p->rx,16);
   }
  }
 }
 return 0;
}
int main(int argc,char**argv){int r=4;struct rlimit core={0,0};unsigned char seed[32],extra;size_t at=0;ssize_t n;
 int service=0;
 if(argc>=2)service=!strcmp(argv[1],"production-enable")?1:!strcmp(argv[1],"service-recover")?3:0;
 if((!service&&argc!=3)||(service&&(argc!=4||strcmp(argv[3],"--fresh-entropy-confirmed")))){fprintf(stderr,"4VRS RNG %s: status|provision|serve PATH; production-enable|service-recover PATH --fresh-entropy-confirmed (32 fresh bytes on stdin)\n",FOURVRS_VERSION);return 2;}
 if(!service&&strcmp(argv[1],"serve")&&strcmp(argv[1],"status")&&strcmp(argv[1],"provision"))return 2;
 if((!strcmp(argv[1],"serve")||service)&&prepare_stop())return 4;
 if(web_exec_close_from(!strcmp(argv[1],"serve")?5:3))return 4;signal(SIGPIPE,SIG_IGN);if(setrlimit(RLIMIT_CORE,&core)||setpriority(PRIO_PROCESS,0,10))return 4;
 if(strcmp(argv[1],"serve"))alarm(30);
 {int opened=rng_nv_open(argv[2]);if(opened){r=opened>0?opened:4;goto done;}}
 r=rng_nv_status();
 if(service){
  if(isatty(0)){r=8;goto done;}
  while(at<32){n=read(0,seed+at,32-at);if(n<=0){r=8;goto done;}at+=(size_t)n;}
  if(read(0,&extra,1)!=0){r=8;goto done;}
  alarm(0);if(stopping){r=8;goto done;}
  r=rng_nv_service(seed,service)?8:0;
  if(!r&&rng_nv_service_finish())r=8;
  if(!r)puts("production-autonomous-v1=prepared startup=automatic threshold=8 enforcement=diagnostic-only");
  goto done;
 }
 if(!strcmp(argv[1],"status")){if(!r&&rng_nv_policy())r=5;goto done;}
 if(!strcmp(argv[1],"provision")){
  if(r!=3||isatty(0)){r=4;goto done;}
  while(at<32){n=read(0,seed+at,32-at);if(n<=0){r=4;goto done;}at+=(size_t)n;}
  if(read(0,&extra,1)!=0||rng_nv_provision(seed)){r=4;goto done;}r=0;puts("provision=complete policy=required");goto done;
 }
 if(stopping){r=0;goto done;}
 if(r)goto done;if(rng_nv_policy()){r=5;goto done;}if(rng_nv_start()){r=rng_nv_error();goto done;}
 r=broker();if(stopping&&(r==21||r==22))r=0;if(!r&&rng_nv_finish())r=8;
done:if(!strcmp(argv[1],"status")){printf("rng=%s schema=%d\n",r==3?"requires-provision":r==4?"invalid-state":r==5?"policy-required":r==6?"CF-unavailable":r==7?"quota-exhausted":r==8?"service-required":r==10?"owner-busy":r?"unavailable":"ready",rng_nv_schema());if(!r||r==5||r==7||r==8)printf("generation=%lu\n",rng_nv_generation());if(rng_nv_production())printf("generations-accounted=%lu observation-threshold=8 threshold-reached=%s enforcement=diagnostic-only\n",rng_nv_used(),rng_nv_used()>=8?"yes":"no");}clear(seed,sizeof(seed));rng_nv_close();if(r)fprintf(stderr,"rng-status=%d\n",r);return r;
}
