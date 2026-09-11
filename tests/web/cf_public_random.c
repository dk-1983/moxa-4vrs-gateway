/* Linked ONLY into fixture workers. Never part of the delivery. */
#include <sys/random.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
ssize_t __wrap_getrandom(void *out,size_t n,unsigned flags){
 const char *mode=getenv("CF_RANDOM");unsigned char *p=out;size_t i;
 if(mode&&!strcmp(mode,"forbidden"))_exit(99);
 if(n!=32||flags!=GRND_NONBLOCK){errno=EINVAL;return -1;}
 if(mode&&!strcmp(mode,"eagain")){errno=EAGAIN;return -1;}
 if(mode&&!strcmp(mode,"enosys")){errno=ENOSYS;return -1;}
 if(mode&&!strcmp(mode,"eintr")){errno=EINTR;return -1;}
 for(i=0;i<n;i++)p[i]=(unsigned char)(i==5?255:i);
 return mode&&!strcmp(mode,"short")?31:32;
}
ssize_t __real_write(int,const void*,size_t);
ssize_t __wrap_write(int fd,const void *p,size_t n){
 const char *mode=getenv("CF_IO");
 if(n==160&&mode&&!strcmp(mode,"short-write"))return __real_write(fd,p,79);
 if(n==160&&mode&&!strcmp(mode,"write-error")){errno=EIO;return -1;}
 return __real_write(fd,p,n);
}
int __real_fsync(int);
int __wrap_fsync(int fd){
 static int count;char name[32];const char *mode=getenv("CF_IO");
 snprintf(name,sizeof(name),"fsync-%d",++count);
 if(mode&&!strcmp(mode,name)){errno=EIO;return -1;}
 return __real_fsync(fd);
}
int __real_close(int);
int __wrap_close(int fd){
 struct stat s;const char *mode=getenv("CF_IO");int fail=mode&&!strcmp(mode,"close-error")&&!fstat(fd,&s)&&s.st_size==160&&S_ISREG(s.st_mode);
 int r=__real_close(fd);if(fail){errno=EIO;return -1;}return r;
}
int __real_renameat2(int,const char*,int,const char*,unsigned);
int __wrap_renameat2(int a,const char *b,int c,const char *d,unsigned flags){
 const char *mode=getenv("CF_IO");
 if(mode&&!strcmp(mode,"rename-error")){errno=EIO;return -1;}
 if(mode&&!strcmp(mode,"rename-collision")){
  int fd=openat(c,d,O_WRONLY|O_CREAT|O_EXCL,0600);
  if(fd<0)return -1;
  if(__real_write(fd,"PUBLIC-existing-state",21)!=21){__real_close(fd);return -1;}
  if(__real_close(fd))return -1;
 }
 return __real_renameat2(a,b,c,d,flags);
}
