#define _GNU_SOURCE
/* One transaction on an explicitly reserved physical console. No device discovery. */
#include "core/monotonic.h"
#include "web/exec_fds.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/sysmacros.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t interrupted;
static void stop(int sig) { interrupted=sig; }
static void wipe(void *p,size_t n) { volatile unsigned char *b=p;while(n--)*b++=0; }
static double now(void) { struct timespec t;if(gateway_monotonic_time(&t)){interrupted=1;return 0;}return t.tv_sec+t.tv_nsec/1e9; }
static int ready(int fd,int writing,double end) {
 fd_set s;struct timeval tv;double left;int r;
 while(!interrupted){left=end-now();if(left<=0)return -1;tv.tv_sec=(long)left;tv.tv_usec=(long)((left-tv.tv_sec)*1e6);
 FD_ZERO(&s);FD_SET(fd,&s);r=select(fd+1,writing?NULL:&s,writing?&s:NULL,NULL,&tv);if(r>0)return 0;if(!r)return -1;if(errno!=EINTR)return -1;}
 return -1;
}
static int transfer(unsigned char *b,size_t n,int writing,double end) {
 size_t at=0;ssize_t r;while(at<n){if(ready(0,writing,end))return -1;r=writing?write(0,b+at,n-at):read(0,b+at,n-at);
 if(r>0)at+=(size_t)r;else if(!r||(errno!=EAGAIN&&errno!=EINTR))return -1;}return 0;
}
static unsigned long crc(const unsigned char *p,size_t n) {
 unsigned long c=0xffffffffUL;unsigned int i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^((c&1)?0xedb88320UL:0);}return c^0xffffffffUL;
}
/* TIOCEXCL alone does not evict existing opens or privileged readers. A failed
 * /proc audit is a refusal, not evidence that the terminal is private. Root
 * service/admin quiescence remains an explicit operational precondition. */
static int sole_reader(dev_t dev) {
 DIR *d=opendir("/proc"),*fds;struct dirent *e,*f;char path[512],base[80],*end;long pid;struct stat st;int bad=0;double limit=now()+2;
 if(!d)return -1;
 for(;;){errno=0;e=readdir(d);if(!e){if(errno)bad=1;break;}if(interrupted||now()>limit){bad=1;break;}
 pid=strtol(e->d_name,&end,10);if(*end||pid<=0||pid==getpid())continue;
 snprintf(base,sizeof(base),"/proc/%ld/fd",pid);fds=opendir(base);if(!fds){if(errno!=ENOENT)bad=1;continue;}
 for(;;){errno=0;f=readdir(fds);if(!f){if(errno)bad=1;break;}if(interrupted||now()>limit){bad=1;break;}
 if(f->d_name[0]=='.')continue;snprintf(path,sizeof(path),"%s/%s",base,f->d_name);
 if(stat(path,&st)){if(errno!=ENOENT)bad=1;continue;}
 if(S_ISCHR(st.st_mode)&&(st.st_rdev==dev||st.st_rdev==makedev(5,1)||st.st_rdev==makedev(5,0)||st.st_rdev==makedev(4,0)))bad=1;}
 closedir(fds);}
 closedir(d);return bad?-1:0;
}
static int helper(const char *exe,const char *state,unsigned char *seed) {
 int p[2],status,nul;pid_t child;double end=now()+35;ssize_t n;
 if(pipe(p))return 7;
 child=fork();if(child<0){close(p[0]);close(p[1]);return 7;}
 if(!child){nul=open("/dev/null",O_RDWR);if(nul<0)_exit(7);
 if(dup2(p[0],0)<0||dup2(nul,1)<0||dup2(nul,2)<0||web_exec_close_from(3))_exit(7);
 {char *args[4];char *env[]={"PATH=/usr/bin:/bin",NULL};args[0]=(char*)exe;args[1]=seed?"provision":"status";args[2]=(char*)state;args[3]=NULL;execve(exe,args,env);}_exit(7);}
 close(p[0]);if(seed){do{n=write(p[1],seed,32);}while(n<0&&errno==EINTR&&!interrupted);if(n!=32){close(p[1]);kill(child,SIGKILL);waitpid(child,&status,0);return 7;}}
 close(p[1]);while(!interrupted&&now()<end){pid_t r=waitpid(child,&status,WNOHANG);struct timeval t={0,10000};if(r==child)return WIFEXITED(status)?WEXITSTATUS(status):7;if(r<0&&errno!=EINTR)break;select(0,NULL,NULL,NULL,&t);}
 kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}return 7;
}
int main(int argc,char **argv) {
 struct stat st,fdst;struct termios saved,raw;struct rlimit zero={0,0};struct sigaction sa;
 unsigned char frame[44],finish[8],msg[8]={'R','B','S','1',0,0,0,0};unsigned long checksum;int mode,r=7,exclusive=0,changed=0,flags=-1,i;double end;
 const char *state="/var/hda/4vrs-rng";
#ifdef WEB_HOST_TEST
 if(argc==6)state=argv[5];else
#endif
 if(argc!=5)return 2;
 /* Literal consent flag documents supervision preflight; it cannot replace it. */
 if(strcmp(argv[4],"--reserved-console")||argv[2][0]!='/'||argv[3][0]!='/')return 2;
 mode=!strcmp(argv[1],"probe")?'T':!strcmp(argv[1],"provision")?'P':!strcmp(argv[1],"status")?'S':0;if(!mode)return 2;
 if(geteuid()!=0||getpid()!=getsid(0)||tcgetpgrp(0)!=getpgrp()||getpgrp()!=getpid())return 7;
 if(lstat(argv[2],&st)||fstat(0,&fdst)||!S_ISCHR(st.st_mode)||st.st_rdev!=fdst.st_rdev||!isatty(0))return 7;
 if(st.st_rdev==makedev(5,0)||st.st_rdev==makedev(5,1)||st.st_rdev==makedev(4,0))return 7;
 /* Never select an application UART, including aliases of ttyM0..ttyM7. */
 for(i=0;i<8;i++){char path[32];struct stat uart;snprintf(path,sizeof(path),"/dev/ttyM%d",i);
 if(!stat(path,&uart)){if(uart.st_rdev==st.st_rdev)return 7;}else if(errno!=ENOENT)return 7;}
 if(web_exec_close_from(3)||setrlimit(RLIMIT_CORE,&zero)||tcgetattr(0,&saved)||sole_reader(st.st_rdev))return 7;
 memset(&sa,0,sizeof(sa));sa.sa_handler=stop;sigemptyset(&sa.sa_mask);sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);sigaction(SIGHUP,&sa,NULL);signal(SIGPIPE,SIG_IGN);
 if(ioctl(0,TIOCEXCL))goto done;
 exclusive=1;if(sole_reader(st.st_rdev))goto done;
 raw=saved;cfmakeraw(&raw);raw.c_cflag|=CLOCAL|CREAD;raw.c_cflag&=~(PARENB|CSTOPB|CSIZE|CRTSCTS);raw.c_cflag|=CS8;
 if(cfsetispeed(&raw,B115200)||cfsetospeed(&raw,B115200))goto done;
 flags=fcntl(0,F_GETFL);if(flags<0||fcntl(0,F_SETFL,flags|O_NONBLOCK))goto done;
 if(tcsetattr(0,TCSANOW,&raw))goto done;
 changed=1;if(tcflush(0,TCIFLUSH))goto done;
 /* Serial has no pipe EOF. The public greeting also lets the desktop open
  * after the operator relinquishes their console terminal application. */
 end=now()+30;if(transfer(finish,8,0,end)||memcmp(finish,"RBH1",4)||finish[4]!=mode||finish[5]||finish[6]||finish[7])goto done;
 msg[4]=(unsigned char)mode;msg[5]=(unsigned char)(mode=='T'?0:helper(argv[3],state,NULL));
 if(transfer(msg,8,1,now()+3))goto done;
 if(mode=='S'){r=0;goto done;}if(mode=='P'&&msg[5]!=3)goto done;
 end=now()+10;if(transfer(frame,sizeof(frame),0,end)||transfer(finish,sizeof(finish),0,end))goto reject;
 if(memcmp(frame,"RBF1",4)||frame[4]!=mode||frame[5]||frame[6]||frame[7]!=32||memcmp(finish,"RBE1DONE",8))goto reject;
 checksum=((unsigned long)frame[40]<<24)|((unsigned long)frame[41]<<16)|((unsigned long)frame[42]<<8)|frame[43];if(checksum!=crc(frame,40))goto reject;
 if(mode=='T')for(i=0;i<32;i++){unsigned char expected=(unsigned char)i;if(i==5)expected=255;if(frame[8+i]!=expected)goto reject;}
 /* Acceptance boundary: full validated frame/finish, then 250ms input quiet.
  * Bytes after this boundary cannot undo a durable helper commit. */
 {fd_set s;struct timeval t={0,250000};int quiet;FD_ZERO(&s);FD_SET(0,&s);quiet=select(1,&s,NULL,NULL,&t);if(quiet!=0||interrupted)goto reject;}
 {int queued;if(ioctl(0,FIONREAD,&queued)||queued||sole_reader(st.st_rdev))goto reject;}
 r=mode=='T'?0:helper(argv[3],state,frame+8);
reject:
 memcpy(msg,"RBA1",4);msg[4]=(unsigned char)r;msg[5]=msg[6]=msg[7]=0;
 if(transfer(msg,8,1,now()+3))r=7;
done:
 wipe(frame,sizeof(frame));wipe(finish,sizeof(finish));
 /* Also drops late serial bytes before a supervisor may start a new login. */
 if(changed){if(tcflush(0,TCIFLUSH))r=7;if(tcsetattr(0,TCSANOW,&saved))r=7;}
 if(flags>=0&&fcntl(0,F_SETFL,flags))r=7;
 if(exclusive&&ioctl(0,TIOCNXCL))r=7;
 return r;
}
