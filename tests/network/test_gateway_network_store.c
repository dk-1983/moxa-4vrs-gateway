#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "network/gateway_network_store.h"

static unsigned int checks,failed;
static int fail_at,crash_at,calls;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
int __real_fsync(int);
int __real_rename(const char *,const char *);
static int fault(void)
{++calls;if(calls==crash_at)_exit(71);if(calls==fail_at){errno=EIO;return 1;}return 0;}
int __wrap_fsync(int fd){return fault()?-1:__real_fsync(fd);}
int __wrap_rename(const char *a,const char *b){return fault()?-1:__real_rename(a,b);}
static void clean(const char *dir)
{const char *names[]={"confirmed","good","candidate","write.tmp","lock","commit.guard"};unsigned int i;char p[256];for(i=0;i<6U;++i){snprintf(p,sizeof(p),"%s/%s",dir,names[i]);unlink(p);}rmdir(dir);}
static int value(const char *dir,const char *expected)
{char b[128];size_t n;return gateway_network_store_boot(dir,b,sizeof(b),&n)==0&&n==strlen(expected)&&!memcmp(b,expected,n);}
static void scenario(int at,int crash)
{
    char dir[]="/tmp/4vrs-net-store-XXXXXX";pid_t pid;int status;char b[128];size_t n;
    CHECK(mkdtemp(dir)!=0);
    CHECK(gateway_network_store_adopt(dir,"old network AND bindings",24)==0);
    CHECK(gateway_network_store_write(dir,"candidate","new network AND bindings",24)==0);
    pid=fork();CHECK(pid>=0);
    if(pid==0){int fd=gateway_network_store_lock(dir);if(fd<0)_exit(99);calls=0;if(crash)crash_at=at;else fail_at=at;(void)gateway_network_store_confirm(dir);gateway_network_store_unlock(fd);_exit(0);}
    if(pid>0){CHECK(waitpid(pid,&status,0)==pid);CHECK(WIFEXITED(status));}
    /* Any interrupted fsync/rename boundary yields one entire generation,
     * never a hybrid of new LAN policy and old listener affinity. */
    CHECK(gateway_network_store_boot(dir,b,sizeof(b),&n)==0);
    CHECK(n==24 && (!memcmp(b,"old network AND bindings",24)||!memcmp(b,"new network AND bindings",24)));
    /* Before publication (the final directory sync), guard must select old.
     * After a one-shot error at that sync, guard is recreated before return. */
    if(at<=10||!crash)CHECK(value(dir,"old network AND bindings"));
    calls=fail_at=crash_at=0;CHECK(!gateway_network_store_abort_commit(dir));
    clean(dir);
}
int main(void)
{
    char dir[]="/tmp/4vrs-net-store-XXXXXX",p[256],b[128];size_t n;int fd,lock,status;pid_t pid;unsigned int i;
    CHECK(mkdtemp(dir)!=0);lock=gateway_network_store_lock(dir);CHECK(lock>=0);
    CHECK(gateway_network_store_boot(dir,b,sizeof(b),&n)==GATEWAY_NET_STORE_ABSENT);
    CHECK(gateway_network_store_adopt(dir,"baseline",8)==0);
    CHECK(gateway_network_store_adopt(dir,"accidental reset",16)!=0);
    CHECK(gateway_network_store_write(dir,"candidate","unconfirmed",11)==0);
    CHECK(value(dir,"baseline"));
    pid=fork();CHECK(pid>=0);
    if(pid==0){int other=gateway_network_store_lock(dir);_exit(other<0?0:1);}
    if(pid>0){CHECK(waitpid(pid,&status,0)==pid);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);}
    CHECK(gateway_network_store_confirm(dir)==0);CHECK(value(dir,"unconfirmed"));
    CHECK(gateway_network_store_read(dir,"candidate",b,sizeof(b),&n)==GATEWAY_NET_STORE_ABSENT);
    snprintf(p,sizeof(p),"%s/confirmed",dir);fd=open(p,O_WRONLY);CHECK(fd>=0);
    if(fd>=0){CHECK(write(fd,"bad",3)==3);close(fd);}
    CHECK(value(dir,"baseline")); /* CRC failure selects known-good only. */
    CHECK(gateway_network_store_write(dir,"candidate","never boot this",15)==0);
    CHECK(value(dir,"baseline"));
    CHECK(gateway_network_store_read(dir,"../escape",b,sizeof(b),&n)==GATEWAY_NET_STORE_INVALID);
    snprintf(p,sizeof(p),"%s/write.tmp",dir);CHECK(symlink("confirmed",p)==0);
    CHECK(gateway_network_store_write(dir,"confirmed","oops",4)==GATEWAY_NET_STORE_IO);
    CHECK(value(dir,"baseline"));
    gateway_network_store_unlock(lock);clean(dir);
    for(i=1;i<=11U;++i){scenario((int)i,0);scenario((int)i,1);}
    printf("network store checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
