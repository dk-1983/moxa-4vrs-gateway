/* Link ONLY into host/UBSan fault binaries; never into the target artifact. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
static int random_fd=-1;
static const char *fault(void){const char *s=getenv("PROBE_FAULT");return s?s:"";}
int __real_open(const char *,int,...);
ssize_t __real_read(int,void *,size_t);
int __real_close(int);
int __real_setpriority(__priority_which_t,id_t,int);
int __wrap_setpriority(__priority_which_t which,id_t who,int value){if(!strcmp(fault(),"priority")){errno=EPERM;return -1;}return __real_setpriority(which,who,value);}
int __wrap_open(const char *path,int flags,...)
{
    if(!strcmp(path,"/dev/urandom"))abort(); /* No unnoticed fallback. */
    if(!strcmp(path,"/dev/random")){
        assert(flags&O_NONBLOCK);
        if(!strcmp(fault(),"open")){errno=EACCES;return -1;}
        random_fd=__real_open(path,flags);return random_fd;
    }
    if(flags&O_CREAT){va_list ap;mode_t mode;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);return __real_open(path,flags,mode);}
    return __real_open(path,flags);
}
ssize_t __wrap_read(int fd,void *p,size_t n)
{
    if(fd==random_fd){
        if(!strcmp(fault(),"eagain")){errno=EAGAIN;return -1;}
        if(!strcmp(fault(),"eintr")){errno=EINTR;return -1;}
        if(!strcmp(fault(),"eio")){errno=EIO;return -1;}
        if(!strcmp(fault(),"eof"))return 0;
        if(!strcmp(fault(),"short")){memset(p,0xA5,n/2);return n/2;}
        if(!strcmp(fault(),"alarm")){raise(SIGALRM);abort();}
    }
    return __real_read(fd,p,n);
}
int __wrap_close(int fd){int r=__real_close(fd);if(fd==random_fd){random_fd=-1;errno=EBADF;}return r;}
