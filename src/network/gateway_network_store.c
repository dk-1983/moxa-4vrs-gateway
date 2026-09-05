#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "network/gateway_network_store.h"

#define PATH_MAX_BYTES 256U
#define HEADER_SIZE 16U

static int path(char *out, const char *dir, const char *name)
{
    int n;
    if (!dir || !name || (strcmp(name,"confirmed") && strcmp(name,"good") &&
        strcmp(name,"candidate") && strcmp(name,"commit.guard") && strcmp(name,"write.tmp") && strcmp(name,"lock"))) return -1;
    n=snprintf(out,PATH_MAX_BYTES,"%s/%s",dir,name);
    return n<0 || (size_t)n>=PATH_MAX_BYTES ? -1 : 0;
}
static unsigned long crc(const unsigned char *s, size_t n)
{
    unsigned long value=0xffffffffUL; unsigned int i;
    while (n--) { value^=*s++; for (i=0;i<8U;++i) value=(value>>1)^((value&1UL)?0xedb88320UL:0UL); }
    return (value^0xffffffffUL)&0xffffffffUL;
}
static void put32(unsigned char *p,unsigned long v)
{ p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v; }
static unsigned long get32(const unsigned char *p)
{ return ((unsigned long)p[0]<<24)|((unsigned long)p[1]<<16)|((unsigned long)p[2]<<8)|p[3]; }
static int transfer(int fd, void *buffer, size_t n, int writing)
{
    unsigned char *p=buffer;
    while (n) {
        ssize_t r=writing?write(fd,p,n):read(fd,p,n);
        if (r<0 && errno==EINTR) continue;
        if (r<=0) return -1;
        p+=(size_t)r;n-=(size_t)r;
    }
    return 0;
}
static int regular_open(const char *p,int flags)
{
    struct stat before,after;
    int existed=lstat(p,&before)==0,fd;
    if (existed && (!S_ISREG(before.st_mode) || before.st_nlink!=1)) return -1;
    if (!existed && errno!=ENOENT) return -1;
    fd=open(p,flags,0600);
    if (fd<0) return -1;
    if (fstat(fd,&after) || !S_ISREG(after.st_mode) || after.st_nlink!=1 ||
        (existed && (after.st_dev!=before.st_dev || after.st_ino!=before.st_ino))) {close(fd);return -1;}
    if (fcntl(fd,F_SETFD,FD_CLOEXEC)<0) {close(fd);return -1;}
    return fd;
}
static int sync_directory(const char *dir)
{int fd=open(dir,O_RDONLY),r;if(fd<0)return -1;r=fsync(fd);if(close(fd))r=-1;return r;}

int gateway_network_store_lock(const char *dir)
{
    char p[PATH_MAX_BYTES];struct flock lock;int fd;
    if(path(p,dir,"lock"))return -1;
    fd=regular_open(p,O_RDWR|O_CREAT);if(fd<0)return -1;
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fcntl(fd,F_SETLK,&lock)<0){close(fd);return -1;}return fd;
}
void gateway_network_store_unlock(int fd){if(fd>=0)close(fd);}

gateway_network_store_result_t gateway_network_store_read(const char *dir,const char *name,
                                                          char *out,size_t cap,size_t *length)
{
    char p[PATH_MAX_BYTES];unsigned char header[HEADER_SIZE],tail;
    struct stat st;unsigned long n;int fd,r;
    if(length)*length=0;
    if(!out||!length||path(p,dir,name))return GATEWAY_NET_STORE_INVALID;
    if(lstat(p,&st)<0)return errno==ENOENT?GATEWAY_NET_STORE_ABSENT:GATEWAY_NET_STORE_IO;
    fd=regular_open(p,O_RDONLY);if(fd<0)return GATEWAY_NET_STORE_IO;
    r=transfer(fd,header,sizeof(header),0);
    if(r||memcmp(header,"4VRSNET1",8)){close(fd);return GATEWAY_NET_STORE_INVALID;}
    n=get32(header+8);
    if(!n||n>GATEWAY_NETWORK_SNAPSHOT_MAX||n>cap){close(fd);return GATEWAY_NET_STORE_INVALID;}
    r=transfer(fd,out,(size_t)n,0);
    if(r||read(fd,&tail,1)!=0||get32(header+12)!=crc((unsigned char*)out,(size_t)n)){
        close(fd);return GATEWAY_NET_STORE_INVALID;
    }
    if(close(fd))return GATEWAY_NET_STORE_IO;
    *length=(size_t)n;return GATEWAY_NET_STORE_OK;
}

gateway_network_store_result_t gateway_network_store_write(const char *dir,const char *name,
                                                           const char *data,size_t length)
{
    char p[PATH_MAX_BYTES],tmp[PATH_MAX_BYTES];unsigned char header[HEADER_SIZE];int fd,r;
    if(!data||!length||length>GATEWAY_NETWORK_SNAPSHOT_MAX||path(p,dir,name)||
        path(tmp,dir,"write.tmp")||!strcmp(name,"write.tmp")||!strcmp(name,"lock"))return GATEWAY_NET_STORE_INVALID;
    /* A crashed writer may leave a temporary file. Only this reserved regular
     * file is replaceable; symlinks and unrelated paths are never followed. */
    fd=regular_open(tmp,O_WRONLY|O_CREAT);if(fd<0)return GATEWAY_NET_STORE_IO;
    memcpy(header,"4VRSNET1",8);put32(header+8,(unsigned long)length);put32(header+12,crc((const unsigned char*)data,length));
    r=ftruncate(fd,0)||transfer(fd,header,sizeof(header),1)||transfer(fd,(void*)data,length,1)||fsync(fd);
    if(close(fd))r=-1;
    if(r)return GATEWAY_NET_STORE_IO;
    if(rename(tmp,p))return GATEWAY_NET_STORE_IO;
    return sync_directory(dir)?GATEWAY_NET_STORE_UNCERTAIN:GATEWAY_NET_STORE_OK;
}

gateway_network_store_result_t gateway_network_store_adopt(const char *dir,const char *data,size_t n)
{
    char p[PATH_MAX_BYTES];struct stat st;gateway_network_store_result_t r;
    if(path(p,dir,"confirmed"))return GATEWAY_NET_STORE_INVALID;
    if(lstat(p,&st)==0 || errno!=ENOENT)return GATEWAY_NET_STORE_INVALID;
    if(path(p,dir,"good"))return GATEWAY_NET_STORE_INVALID;
    if(lstat(p,&st)==0 || errno!=ENOENT)return GATEWAY_NET_STORE_INVALID;
    r=gateway_network_store_write(dir,"good",data,n);
    return r==GATEWAY_NET_STORE_OK?gateway_network_store_write(dir,"confirmed",data,n):r;
}

gateway_network_store_result_t gateway_network_store_boot(const char *dir,char *out,size_t cap,size_t *n)
{
    gateway_network_store_result_t r=gateway_network_store_read(dir,"commit.guard",out,cap,n);
    if(r!=GATEWAY_NET_STORE_ABSENT)return r;
    r=gateway_network_store_read(dir,"confirmed",out,cap,n);
    if(r==GATEWAY_NET_STORE_OK)return r;
    return gateway_network_store_read(dir,"good",out,cap,n);
}

gateway_network_store_result_t gateway_network_store_confirm(const char *dir)
{
    char current[GATEWAY_NETWORK_SNAPSHOT_MAX],candidate[GATEWAY_NETWORK_SNAPSHOT_MAX];
    size_t a,b;gateway_network_store_result_t r;
    r=gateway_network_store_boot(dir,current,sizeof(current),&a);if(r)return r;
    r=gateway_network_store_read(dir,"candidate",candidate,sizeof(candidate),&b);if(r)return r;
    r=gateway_network_store_write(dir,"commit.guard",current,a);if(r)return r;
    r=gateway_network_store_write(dir,"good",current,a);if(r)return r;
    r=gateway_network_store_write(dir,"confirmed",candidate,b);if(r)return r;
    r=gateway_network_store_discard(dir);
    if(r!=GATEWAY_NET_STORE_OK)return r;
    {char guard[PATH_MAX_BYTES];if(path(guard,dir,"commit.guard")||unlink(guard))return GATEWAY_NET_STORE_IO;
        if(sync_directory(dir)){
            /* Reestablish rollback authority after an uncertain publication.
             * Permanent storage failure remains explicit, never KEPT. */
            (void)gateway_network_store_write(dir,"commit.guard",current,a);
            return GATEWAY_NET_STORE_UNCERTAIN;
        }}
    return GATEWAY_NET_STORE_OK;
}

gateway_network_store_result_t gateway_network_store_discard(const char *dir)
{
    char p[PATH_MAX_BYTES];
    if(path(p,dir,"candidate"))return GATEWAY_NET_STORE_INVALID;
    if(unlink(p)&&errno!=ENOENT)return GATEWAY_NET_STORE_IO;
    return sync_directory(dir)?GATEWAY_NET_STORE_UNCERTAIN:GATEWAY_NET_STORE_OK;
}

/* Caller holds the transaction lock and has reaped every prior writer. */
gateway_network_store_result_t gateway_network_store_abort_commit(const char *dir)
{
 char old[GATEWAY_NETWORK_SNAPSHOT_MAX],guard[PATH_MAX_BYTES];size_t n;gateway_network_store_result_t r;
 r=gateway_network_store_read(dir,"commit.guard",old,sizeof(old),&n);
 if(r==GATEWAY_NET_STORE_ABSENT)return GATEWAY_NET_STORE_OK;
 if(r)return r;
 r=gateway_network_store_write(dir,"confirmed",old,n);if(r)return r;
 if(path(guard,dir,"commit.guard")||unlink(guard))return GATEWAY_NET_STORE_IO;
 return sync_directory(dir)?GATEWAY_NET_STORE_UNCERTAIN:GATEWAY_NET_STORE_OK;
}
