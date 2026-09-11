#define _GNU_SOURCE
#include "web/web_ipc_endpoint.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static unsigned int checks;
#define CHECK(x) do{assert(x);checks++;}while(0)
static void path(char *out,const char *dir,unsigned long id){snprintf(out,108,"%s/%lu.sock",dir,id);}
static int raw(const char *p,int listening){struct sockaddr_un a;int fd=socket(AF_UNIX,SOCK_STREAM,0);assert(fd>=0);memset(&a,0,sizeof(a));a.sun_family=AF_UNIX;strcpy(a.sun_path,p);assert(!bind(fd,(struct sockaddr*)&a,sizeof(a)));if(listening)assert(!listen(fd,2));return fd;}
static int unchanged(const char *p,const struct stat *before){struct stat after;return !lstat(p,&after)&&before->st_dev==after.st_dev&&before->st_ino==after.st_ino&&before->st_mode==after.st_mode&&before->st_uid==after.st_uid;}
int main(void){char root[]="/tmp/ipc-regression-XXXXXX",dir[108],p[108],other[108];web_ipc_endpoint_t e,q;struct stat st;int fd,status;pid_t child;unsigned int i;
 CHECK(mkdtemp(root)!=NULL);snprintf(dir,sizeof(dir),"%s/runtime",root);umask(077);
 /* Crash with no cleanup, then exact same endpoint name/id after restart. */
 child=fork();CHECK(child>=0);if(!child){web_ipc_endpoint_init(&e);fd=web_ipc_endpoint_open(&e,dir,945);_exit(fd>=0?0:1);}
 CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 path(p,dir,945);CHECK(!lstat(p,&st)&&S_ISSOCK(st.st_mode));
 web_ipc_endpoint_init(&e);fd=web_ipc_endpoint_open(&e,dir,945);CHECK(fd>=0);
 CHECK((fcntl(fd,F_GETFD)&FD_CLOEXEC)&&(fcntl(e.lock_fd,F_GETFD)&FD_CLOEXEC));
 CHECK(!lstat(p,&st)&&(st.st_mode&0777)==0666);
 child=fork();CHECK(child>=0);if(!child){web_ipc_endpoint_init(&q);_exit(web_ipc_endpoint_open(&q,dir,945)<0?0:1);}
 CHECK(waitpid(child,&status,0)==child&&WEXITSTATUS(status)==0&&unchanged(p,&st));
 web_ipc_endpoint_close(&e,fd);CHECK(lstat(p,&st)<0);
 /* Live foreign listener and bound-but-not-listening endpoints must survive. */
 for(i=0;i<2;i++){
  path(p,dir,946+i);fd=raw(p,(int)i);CHECK(!lstat(p,&st));web_ipc_endpoint_init(&e);
  CHECK(web_ipc_endpoint_open(&e,dir,946+i)<0&&unchanged(p,&st));close(fd);CHECK(!unlink(p));
 }
 for(i=0;i<3;i++){
  path(p,dir,950+i);
  if(i==0){fd=open(p,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);CHECK(write(fd,"foreign",7)==7);close(fd);}
  if(i==1)CHECK(!symlink("/does-not-exist",p));
  if(i==2)CHECK(!mkdir(p,0700));
  CHECK(!lstat(p,&st));web_ipc_endpoint_init(&e);CHECK(web_ipc_endpoint_open(&e,dir,950+i)<0&&unchanged(p,&st));
  if(i==2)CHECK(!rmdir(p));else CHECK(!unlink(p));
 }
 if(geteuid()==0){path(p,dir,954);fd=raw(p,0);close(fd);CHECK(!chown(p,1,1));CHECK(!lstat(p,&st));web_ipc_endpoint_init(&e);CHECK(web_ipc_endpoint_open(&e,dir,954)<0&&unchanged(p,&st));CHECK(!unlink(p));}
 {const char *stages[]={"bind","chmod","listen","nonblock"};
  for(i=0;i<4;i++){web_ipc_endpoint_init(&e);setenv("WEB_IPC_FAIL",stages[i],1);CHECK(web_ipc_endpoint_open(&e,dir,960+i)<0);path(p,dir,960+i);CHECK(lstat(p,&st)<0);unsetenv("WEB_IPC_FAIL");fd=web_ipc_endpoint_open(&e,dir,960+i);CHECK(fd>=0);web_ipc_endpoint_close(&e,fd);CHECK(lstat(p,&st)<0);}
 }
 /* Unlink failure leaves an owned stale inode; next open validates/reclaims it. */
 web_ipc_endpoint_init(&e);fd=web_ipc_endpoint_open(&e,dir,970);CHECK(fd>=0);path(p,dir,970);setenv("WEB_IPC_FAIL","cleanup",1);web_ipc_endpoint_close(&e,fd);CHECK(!lstat(p,&st));unsetenv("WEB_IPC_FAIL");
 setenv("WEB_IPC_FAIL","proc",1);CHECK(web_ipc_endpoint_open(&e,dir,970)<0&&unchanged(p,&st));unsetenv("WEB_IPC_FAIL");fd=web_ipc_endpoint_open(&e,dir,970);CHECK(fd>=0);web_ipc_endpoint_close(&e,fd);
 /* Cleanup must not unlink a replacement, even if it is another socket. */
 for(i=0;i<3;i++){
  web_ipc_endpoint_init(&e);fd=web_ipc_endpoint_open(&e,dir,980+i);CHECK(fd>=0);path(p,dir,980+i);snprintf(other,sizeof(other),"%s/preserved",dir);CHECK(!rename(p,other));
  if(i==0){status=open(p,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(status>=0);close(status);}
  if(i==1)CHECK(!symlink(other,p));
  if(i==2){status=raw(p,1);}
  CHECK(!lstat(p,&st));web_ipc_endpoint_close(&e,fd);CHECK(unchanged(p,&st));if(i==2)close(status);CHECK(!unlink(p));CHECK(!unlink(other));
 }
 snprintf(other,sizeof(other),"%s/foreign-dir",root);CHECK(!mkdir(other,0777));CHECK(!chmod(other,0777));CHECK(!lstat(other,&st));web_ipc_endpoint_init(&e);CHECK(web_ipc_endpoint_open(&e,other,1)<0&&unchanged(other,&st));CHECK(!rmdir(other));
 snprintf(other,sizeof(other),"%s/alias",root);CHECK(!symlink(dir,other));CHECK(!lstat(other,&st));CHECK(web_ipc_endpoint_open(&e,other,1)<0&&unchanged(other,&st));CHECK(!unlink(other));
 /* Remove only this test's exact lock names; never a production wildcard. */
 {const unsigned long ids[]={945,946,947,950,951,952,954,960,961,962,963,970,980,981,982};for(i=0;i<sizeof(ids)/sizeof(ids[0]);i++){snprintf(p,sizeof(p),"%s/%lu.lock",dir,ids[i]);if(access(p,F_OK)==0)CHECK(!unlink(p));}}
 CHECK(!rmdir(dir));CHECK(!rmdir(root));printf("PASS IPC endpoint: %u checks, crash/name reuse, live/bound endpoint, foreign objects, partial bind and identity cleanup\n",checks);return 0;
}
