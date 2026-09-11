#define _GNU_SOURCE
#include "installer/install_process.h"
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{n++;if(!(x)){fprintf(stderr,"process line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned int n;
typedef struct detached_work {char path[1024];int lock;} detached_work_t;
static int detached(void*v){detached_work_t*w=v;int fd;usleep(300000);
 if(getsid(0)!=getpid()||fcntl(123,F_GETFD)!=-1||fcntl(w->lock,F_GETFD)<0)return 1;
 fd=open(w->path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return 1;
 if(write(fd,"completed",9)!=9){close(fd);return 1;}return close(fd);
}
int main(int argc,char**argv){pid_t child;int status,fd;install_process_t identity,wrong;char own[1024],*args[4];ssize_t z;
 if(argc==2&&!strcmp(argv[1],"--isolated"))return fcntl(123,F_GETFD)==-1?0:1;
 if(argc==3&&!strcmp(argv[1],"--late-writer")){char ready[1100];child=fork();if(child<0)return 1;if(!child){usleep(700000);fd=open(argv[2],O_WRONLY|O_CREAT|O_EXCL,0600);if(fd>=0){(void)write(fd,"late",4);close(fd);}_exit(0);}snprintf(ready,sizeof(ready),"%s.ready",argv[2]);fd=open(ready,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return 1;if(write(fd,"armed",5)!=5)return 1;close(fd);for(;;)pause();}
 CHECK(argc==2);
 z=readlink("/proc/self/exe",own,sizeof(own)-1);CHECK(z>0&&(size_t)z<sizeof(own));own[z]=0;
 child=fork();CHECK(child>=0);if(!child){for(;;)pause();}
 CHECK(!install_process_capture(child,own,&identity));wrong=identity;wrong.starttime++;
 CHECK(install_process_stop(&wrong,100)==-1);CHECK(kill(child,0)==0);
 CHECK(!install_process_stop(&identity,1000));CHECK(waitpid(child,&status,0)==child);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGTERM);
 fd=open("/dev/null",O_RDONLY);CHECK(fd>=0);CHECK(dup2(fd,123)==123);close(fd);
 args[0]=own;args[1]="--isolated";args[2]=0;CHECK(install_process_run(own,args,1000)==0);CHECK(fcntl(123,F_GETFD)>=0);close(123);
 args[0]="/bin/sleep";args[1]="5";args[2]=0;CHECK(install_process_run(args[0],args,20)==-2);
 args[0]="/nonexistent-installer-test";args[1]=0;CHECK(install_process_run(args[0],args,1000)==126);
 {char path[1024],ready[1100],dir[1024];CHECK(snprintf(dir,sizeof(dir),"%s/process-XXXXXX",argv[1])>0);CHECK(mkdtemp(dir)!=0);CHECK(strlen(dir)+6<sizeof(path));strcpy(path,dir);strcat(path,"/late");CHECK(access(path,F_OK)!=0);args[0]=own;args[1]="--late-writer";args[2]=path;args[3]=0;CHECK(install_process_run(own,args,200)==-2);snprintf(ready,sizeof(ready),"%s.ready",path);CHECK(access(ready,F_OK)==0);usleep(800000);CHECK(access(path,F_OK)!=0);}
 {detached_work_t w;char dir[1024];int pipefd[2],tries;pid_t launcher,worker;
  CHECK(snprintf(dir,sizeof(dir),"%s/detached-XXXXXX",argv[1])>0);CHECK(mkdtemp(dir)!=0);CHECK(strlen(dir)+8<sizeof(w.path));strcpy(w.path,dir);strcat(w.path,"/result");
  CHECK(!pipe(pipefd));w.lock=open("/dev/null",O_RDONLY);CHECK(w.lock>=3);CHECK(dup2(w.lock,123)==123);
  launcher=fork();CHECK(launcher>=0);
  if(!launcher){close(pipefd[0]);if(install_process_detach(w.lock,detached,&w,&worker))_exit(90);if(write(pipefd[1],&worker,sizeof(worker))!=sizeof(worker))_exit(91);_exit(0);}
  close(pipefd[1]);CHECK(read(pipefd[0],&worker,sizeof(worker))==sizeof(worker));close(pipefd[0]);close(w.lock);close(123);
  CHECK(waitpid(launcher,&status,0)==launcher);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
  CHECK(!install_process_capture(worker,own,&identity));CHECK(install_process_matches(&identity)==1);CHECK(!kill(worker,SIGHUP));
  for(tries=0;tries<200&&access(w.path,F_OK);tries++)usleep(10000);
  CHECK(tries<200);fd=open(w.path,O_RDONLY);CHECK(fd>=0);{char b[10]={0};CHECK(read(fd,b,10)==9&&!strcmp(b,"completed"));}close(fd);
 }
 printf("installer process: %u checks passed; only test children signaled\n",n);return 0;
}
