#define _GNU_SOURCE
/* Host-only LD_PRELOAD probe. No entropy, paths or credentials are logged. */
#include <dlfcn.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static long ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000;}
__attribute__((constructor)) static void early(void){
 char exe[512];ssize_t n;const char *file=getenv("RNG_SLOW_FILE");
 if(!file||strcmp(file,"preexec"))return;
 n=readlink("/proc/self/exe",exe,sizeof(exe)-1);if(n<=0)return;exe[n]=0;
 if(!strrchr(exe,'/')||strcmp(strrchr(exe,'/')+1,"4vrs-rng"))return;
 {sigset_t set;struct timespec t={4,500000000};sigprocmask(SIG_SETMASK,NULL,&set);
 fprintf(stderr,"probe slow-begin pid=%ld ms=%ld term-handler=0 term-blocked=%d\n",(long)getpid(),ms(),sigismember(&set,SIGTERM));fflush(stderr);
 while(nanosleep(&t,&t)&&errno==EINTR){}
 fprintf(stderr,"probe slow-end pid=%ld ms=%ld\n",(long)getpid(),ms());fflush(stderr);}
}
int fsync(int fd){
 int (*real)(int)=dlsym(RTLD_NEXT,"fsync");char path[64],name[512];ssize_t n;static int delayed,seen;
 const char *file=getenv("RNG_SLOW_FILE"),*delay=getenv("RNG_SLOW_MS");
 snprintf(path,sizeof(path),"/proc/self/fd/%d",fd);n=readlink(path,name,sizeof(name)-1);
 if(n>0){char *base;name[n]=0;base=strrchr(name,'/');
  if(!delayed&&file&&delay&&base&&!strcmp(base+1,file)&&++seen==(getenv("RNG_SLOW_HIT")?atoi(getenv("RNG_SLOW_HIT")):1)){
   struct sigaction sa;sigset_t set;struct timespec t;long end=ms()+strtol(delay,NULL,10);delayed=1;
   sigaction(SIGTERM,NULL,&sa);
   sigprocmask(SIG_SETMASK,NULL,&set);
   fprintf(stderr,"probe slow-begin pid=%ld ms=%ld term-handler=%d term-blocked=%d\n",(long)getpid(),ms(),sa.sa_handler!=SIG_DFL&&sa.sa_handler!=SIG_IGN,sigismember(&set,SIGTERM));fflush(stderr);
   while(ms()<end){long left=end-ms();t.tv_sec=left/1000;t.tv_nsec=(left%1000)*1000000;while(nanosleep(&t,&t)&&errno==EINTR){}}
   fprintf(stderr,"probe slow-end pid=%ld ms=%ld\n",(long)getpid(),ms());fflush(stderr);
  }
 }
 return real(fd);
}
int kill(pid_t pid,int sig){int(*real)(pid_t,int)=dlsym(RTLD_NEXT,"kill");
 fprintf(stderr,"probe kill pid=%ld sig=%d ms=%ld\n",(long)pid,sig,ms());fflush(stderr);return real(pid,sig);}
pid_t waitpid(pid_t pid,int *status,int options){pid_t(*real)(pid_t,int*,int)=dlsym(RTLD_NEXT,"waitpid");pid_t r=real(pid,status,options);
 if(r>0&&status){fprintf(stderr,"probe reaped pid=%ld signal=%d exit=%d ms=%ld\n",(long)r,WIFSIGNALED(*status)?WTERMSIG(*status):0,WIFEXITED(*status)?WEXITSTATUS(*status):-1,ms());fflush(stderr);}return r;}
