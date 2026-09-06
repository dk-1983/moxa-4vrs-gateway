#define _GNU_SOURCE
#include "installer/install_process.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int read_identity(pid_t pid,install_process_t*out){
 char path[64],buf[2048],*p,*end;struct stat s;int fd;ssize_t n;unsigned int field;
 if(pid<=1)return -1;
 snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);fd=open(path,O_RDONLY);if(fd<0)return errno==ENOENT?0:-1;
 n=read(fd,buf,sizeof(buf)-1);if(close(fd)||n<=0||n==(ssize_t)sizeof(buf)-1)return -1;buf[n]=0;
 p=strrchr(buf,')');if(!p||p[1]!=' '||!p[2]||p[3]!=' ')return -1;
 if(p[2]=='Z')return 0;
 p+=4;
 for(field=4;field<22;field++){p=strchr(p,' ');if(!p)return -1;p++;}
 errno=0;out->starttime=strtoull(p,&end,10);if(errno||end==p||*end!=' ')return -1;
 snprintf(path,sizeof(path),"/proc/%ld/exe",(long)pid);if(stat(path,&s))return errno==ENOENT?0:-1;
 if(!S_ISREG(s.st_mode)||s.st_uid!=geteuid())return -1;
 out->pid=pid;out->device=s.st_dev;out->inode=s.st_ino;return 1;
}
int install_process_capture(pid_t pid,const char*executable,install_process_t*out){struct stat s;int r;if(!executable||stat(executable,&s)||!S_ISREG(s.st_mode)||s.st_uid!=geteuid())return -1;r=read_identity(pid,out);return r==1&&out->device==s.st_dev&&out->inode==s.st_ino?0:-1;}
int install_process_matches(const install_process_t*expected){install_process_t actual;int r=read_identity(expected->pid,&actual);if(r<=0)return r;return actual.device==expected->device&&actual.inode==expected->inode&&actual.starttime==expected->starttime?1:-1;}
static unsigned long long now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(unsigned long long)t.tv_sec*1000U+(unsigned long long)t.tv_nsec/1000000U;}
static void pause_ms(void){struct timespec t;t.tv_sec=0;t.tv_nsec=10000000L;(void)nanosleep(&t,0);}
int install_process_stop(const install_process_t*p,unsigned int timeout){
 unsigned long long start=now();int r;if(!start||timeout>60000||!timeout)return -1;
 r=install_process_matches(p);if(!r)return 0;if(r!=1||kill(p->pid,SIGTERM))return -1;
 do{r=install_process_matches(p);if(!r)return 0;if(r<0)return -1;pause_ms();}while(now()-start<timeout);
 /* A non-child is never escalated blindly. Leave it running and report the
  * incomplete stop; callers must not publish files or claim rollback. */
 return -2;
}
static int isolate(int keep){DIR*d;struct dirent*e;int held,fd;if(setsid()<0)return -1;d=opendir("/proc/self/fd");if(!d)return -1;held=dirfd(d);for(;;){char*end;long n;errno=0;e=readdir(d);if(!e){if(errno){closedir(d);return -1;}break;}n=strtol(e->d_name,&end,10);if(!*end&&n>=0&&n!=held&&n!=keep)close((int)n);}closedir(d);fd=open("/dev/null",O_RDWR);if(fd<0)return -1;if(fd!=0&&dup2(fd,0)<0)return -1;if(dup2(fd,1)<0||dup2(fd,2)<0)return -1;if(fd>2)close(fd);return 0;}
int install_process_run(const char*exe,char*const args[],unsigned int timeout){
 pid_t child,p;int status;unsigned long long start=now();unsigned int killed=0;
 if(!exe||!args||!start||!timeout||timeout>60000)return -1;
 child=fork();if(child<0)return -1;if(!child){if(isolate(-1))_exit(125);execv(exe,args);_exit(126);}
 for(;;){p=waitpid(child,&status,WNOHANG);if(p==child)return killed?-2:WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);if(p<0&&errno!=EINTR)return -1;
  if(now()-start>=timeout&&!killed){/* This unreaped child cannot have its PID
   * reused. Stop it first, then its private session's original process group;
   * this also covers timeout before the child reaches setsid. No other group
   * can acquire this reserved PID as its leader before waitpid reaps it. */
   if(kill(child,SIGKILL)&&errno!=ESRCH)return -1;
   if(kill(-child,SIGKILL)&&errno!=ESRCH)return -1;
   killed=1;}
  if(killed&&now()-start>=timeout+2000U)return -3;
  pause_ms();
 }
}

int install_process_detach(int lock_fd,int (*work)(void*),void*context,pid_t*out){
 struct sigaction ignore,previous;pid_t child;
 if(lock_fd<3||!work||!out||fcntl(lock_fd,F_GETFD)<0)return -1;
 memset(&ignore,0,sizeof(ignore));ignore.sa_handler=SIG_IGN;sigemptyset(&ignore.sa_mask);
 if(sigaction(SIGHUP,&ignore,&previous))return -1;
 child=fork();
 if(!child){int result;if(isolate(lock_fd))_exit(120);result=work(context);close(lock_fd);_exit(result?1:0);}
 (void)sigaction(SIGHUP,&previous,0);
 if(child<0)return -1;
 *out=child;return 0;
}
