#define _GNU_SOURCE
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Read-only qualification observer. No external commands or signals. */
static int bounded(const char *path,char *b,size_t cap,size_t *length){
 int fd;struct stat s;size_t used=0;ssize_t n;
 fd=open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);if(fd<0)return errno==ENOENT?1:-1;
 if(fstat(fd,&s)||!S_ISREG(s.st_mode)){close(fd);return -1;}
 while(used<cap){n=read(fd,b+used,cap-used);if(n<0&&errno==EINTR)continue;
  if(n<0){close(fd);return -1;}if(!n)break;used+=(size_t)n;}
 if(close(fd)||used==cap)return -1;
 b[used]=0;*length=used;return 0;
}
static int number(const char *s,unsigned long long *v){char *end;
 if(!*s||strspn(s,"0123456789")!=strlen(s))return -1;
 errno=0;*v=strtoull(s,&end,10);return errno||*end?-1:0;
}
static int event_parse(const char *b,size_t length,long *pid,unsigned long long *start){
 char p[32],t[32],exact[384];int end=0,n;unsigned long long value;
 if(sscanf(b,"qualification=publication-v1\nboundary=publish-application\npid=%31[0-9]\nstarttime=%31[0-9]\nexit=77\n%n",p,t,&end)!=2||
    (size_t)end!=length||number(p,&value)||value<2||value>INT_MAX||number(t,start)||!*start)return -1;
 *pid=(long)value;
 n=snprintf(exact,sizeof(exact),"qualification=publication-v1\nboundary=publish-application\npid=%s\nstarttime=%s\nexit=77\n",p,t);
 return n<0||(size_t)n!=length||memcmp(exact,b,length)?-1:0;
}
/* 0 absent, 1 live, 2 zombie, 3 reused, -1 observation failure. */
static int process_state(const char *proc,long pid,unsigned long long expected){
 char path[1024],b[2048],*end,*tail,*token;size_t n;long parsed;int r,i;char state;unsigned long long start;struct stat s;
 if(stat(proc,&s)||!S_ISDIR(s.st_mode))return -1;
 if(snprintf(path,sizeof(path),"%s/%ld/stat",proc,pid)>=(int)sizeof(path))return -1;
 r=bounded(path,b,sizeof(b)-1,&n);
 if(r==1){snprintf(path,sizeof(path),"%s/%ld",proc,pid);return lstat(path,&s)<0&&errno==ENOENT?0:-1;}
 if(r||!n)return -1;
 errno=0;parsed=strtol(b,&end,10);if(errno||parsed!=pid||end[0]!=' '||end[1]!='(')return -1;
 tail=strrchr(end,')');if(!tail||tail[1]!=' ')return -1;tail+=2;
 state=*tail;if(!state||!strchr("RSDZTtWXxIK",state)||tail[1]!=' ')return -1;
 token=tail;
 for(i=3;i<22;i++){token=strchr(token,' ');if(!token)return -1;token++;if(!*token||*token==' ')return -1;}
 end=strchr(token,' ');if(!end)return -1;*end=0;
 if(number(token,&start))return -1;
 if(start!=expected)return 3;
 return state=='Z'?2:1;
}
static int monotonic(unsigned long long *ms){struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
 *ms=(unsigned long long)t.tv_sec*1000U+(unsigned long)t.tv_nsec/1000000U;return 0;
}
static int observe(const char *before,const char *event,const char *proc,unsigned int seconds){
 char old[384],current[384],seen_event[384];size_t oldn,n,seen_n=0;long pid=0;unsigned long long start=0,begin,now;
 int r,seen=0,state=0;struct timespec delay;
 if(!seconds||seconds>60||bounded(before,old,sizeof(old)-1,&oldn)||
    (oldn&&event_parse(old,oldn,&pid,&start))||monotonic(&begin))return 2;
 for(;;){
  r=bounded(event,current,sizeof(current)-1,&n);
  if(r<0)return 2;
  if(!r&&(n!=oldn||memcmp(old,current,n))){
   if(event_parse(current,n,&pid,&start))return 2;
   if(seen&&(n!=seen_n||memcmp(seen_event,current,n)))return 2;
   if(!seen){printf("observer=v2 boundary=publish-application pid=%ld starttime=%llu event_exit=77\n",pid,start);seen=1;seen_n=n;memcpy(seen_event,current,n);}
   state=process_state(proc,pid,start);
   if(state<0)return 2;
   if(state==3){puts("FAIL process=reused");return 4;}
   if(state==0||state==2){printf("OBSERVED process=%s; not proof of power loss or child exit status\n",state==0?"absent":"zombie");return 0;}
  }else if(seen){puts("FAIL event disappeared or reverted");return 2;}
  if(monotonic(&now))return 2;
  if(now-begin>=(unsigned long long)seconds*1000U){printf("FAIL timeout=%s\n",seen?"live-process":"no-new-event");return seen?3:1;}
  delay.tv_sec=0;delay.tv_nsec=100000000L;
  if(nanosleep(&delay,0)&&errno!=EINTR)return 2;
 }
}
#ifndef QUALIFICATION_OBSERVER_TEST
int main(int argc,char **argv){int r;
 if(argc!=2){fprintf(stderr,"usage: 4vrs-qualification-observer event-before.txt\n");return 2;}
 r=observe(argv[1],"/etc/4vrs-installer/qualification-event","/proc",60);
 fprintf(stderr,"observer result=%d\n",r);return r;
}
#else
int main(int argc,char **argv){if(argc!=4)return 2;return observe(argv[1],argv[2],argv[3],1);}
#endif
