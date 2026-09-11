#define _GNU_SOURCE
#include "web/web_ipc_endpoint.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fault(const char *at){
#ifdef WEB_HOST_TEST
 const char *value=getenv("WEB_IPC_FAIL");if(value&&!strcmp(value,at)){errno=EIO;return -1;}
#else
 (void)at;
#endif
 return 0;
}
static int same(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino;}
/* No openat on target libc. Every ancestor is owned/trusted and either not
 * writable by others or sticky; the final directory is strictly owner-writable.
 * This prevents unprivileged pathname replacement between identity checks. */
static int directory(const char *path,int create){
 char part[108];size_t i,n=strlen(path);struct stat st,opened;int fd;
 if(!n||n>=sizeof(part)||path[0]!='/'||strstr(path,"/../")||strstr(path,"/./"))return -1;
 memcpy(part,path,n+1);
 for(i=1;i<n;i++)if(part[i]=='/'){
  part[i]=0;
  if(lstat(part,&st)||!S_ISDIR(st.st_mode)||(st.st_uid!=0&&st.st_uid!=geteuid())||((st.st_mode&0022)&&!(st.st_mode&S_ISVTX)))return -1;
  part[i]='/';
 }
 if(create){mode_t old=umask(0);int made=mkdir(path,0711),saved=errno;umask(old);if(made&&saved!=EEXIST){errno=saved;return -1;}}
 if(lstat(path,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||st.st_gid!=getegid()||(st.st_mode&07777)!=0711)return -1;
 fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0)return -1;
 if(fstat(fd,&opened)||!same(&st,&opened)){close(fd);return -1;}
 return fd;
}
static int endpoint(const char *path,struct stat *st){
 if(lstat(path,st))return -1;
 return S_ISSOCK(st->st_mode)&&st->st_uid==geteuid()&&st->st_gid==getegid()&&st->st_nlink==1?0:-1;
}
/* Includes bound-but-not-listening sockets. Never probe by connecting to an
 * unrelated listener. Unreadable/truncated proc data means no deletion. */
static int inactive(const char *path){
 FILE *f;char line[512],name[108];int safe=1;
 if(fault("proc"))return 0;
 f=fopen("/proc/net/unix","r");if(!f)return 0;
 while(fgets(line,sizeof(line),f)){
  if(!strchr(line,'\n')&&!feof(f)){safe=0;break;}
  if(sscanf(line,"%*s %*s %*s %*s %*s %*s %*s %107s",name)==1&&!strcmp(name,path)){safe=0;break;}
 }
 if(ferror(f))safe=0;if(fclose(f))safe=0;return safe;
}
static int owned_remove(web_ipc_endpoint_t *e){
 struct stat st;int dir,result=-1;
 if(!e->bound)return 0;
 dir=directory(e->directory,0);if(dir<0)return -1;
 if(!lstat(e->path,&st)){
  if(S_ISSOCK(st.st_mode)&&st.st_uid==geteuid()&&st.st_gid==getegid()&&st.st_dev==e->device&&st.st_ino==e->inode)
   result=fault("cleanup")?-1:unlink(e->path);
  /* A replacement belongs to someone else; never remove or chmod it. */
 }else if(errno==ENOENT)result=0;
 close(dir);return result;
}
void web_ipc_endpoint_init(web_ipc_endpoint_t *e){memset(e,0,sizeof(*e));e->lock_fd=-1;}
void web_ipc_endpoint_close(web_ipc_endpoint_t *e,int fd){
 /* Remove only our inode while the owner lock and listening fd are retained. */
 (void)owned_remove(e);if(fd>=0)close(fd);
 if(e->lock_fd>=0)close(e->lock_fd);e->lock_fd=-1;e->bound=0;
}
int web_ipc_endpoint_open(web_ipc_endpoint_t *e,const char *parent,unsigned long id){
 int dir=-1,fd=-1,n;char lock[128];struct stat st,again;struct flock l;struct sockaddr_un address;
 if(e->lock_fd>=0||e->bound)return -1;
 n=snprintf(e->path,sizeof(e->path),"%s/%lu.sock",parent,id);if(n<0||(size_t)n>=sizeof(e->path))return -1;
 if(strlen(parent)>=sizeof(e->directory))return -1;strcpy(e->directory,parent);
 dir=directory(parent,1);if(dir<0)return -1;
 n=snprintf(lock,sizeof(lock),"%s/%lu.lock",parent,id);if(n<0||(size_t)n>=sizeof(lock))goto fail;
 e->lock_fd=open(lock,O_RDWR|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0600);
 if(e->lock_fd<0||fstat(e->lock_fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_gid!=getegid()||(st.st_mode&07777)!=0600||st.st_nlink!=1||st.st_size||fcntl(e->lock_fd,F_SETFD,FD_CLOEXEC))goto fail;
 memset(&l,0,sizeof(l));l.l_type=F_WRLCK;l.l_whence=SEEK_SET;
 if(fcntl(e->lock_fd,F_SETLK,&l))goto fail;
 if(!lstat(e->path,&st)){
  if(endpoint(e->path,&st)||!inactive(e->path)||endpoint(e->path,&again)||!same(&st,&again)||unlink(e->path))goto fail;
 }else if(errno!=ENOENT)goto fail;
 fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)goto fail;
 if(fcntl(fd,F_SETFD,FD_CLOEXEC))goto fail;
 memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;strcpy(address.sun_path,e->path);
 if(bind(fd,(struct sockaddr*)&address,sizeof(address)))goto fail;
 if(endpoint(e->path,&st))goto fail;
 e->bound=1;e->device=st.st_dev;e->inode=st.st_ino;
 if(fault("bind")||chmod(e->path,0666)||fault("chmod")||listen(fd,2)||fault("listen")||fcntl(fd,F_SETFL,O_NONBLOCK)||fault("nonblock"))goto fail;
 close(dir);return fd;
fail:
 web_ipc_endpoint_close(e,fd);if(dir>=0)close(dir);return -1;
}
