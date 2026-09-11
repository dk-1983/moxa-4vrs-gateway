#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "network/gateway_network_supervisor.h"
#include "network/gateway_network_store.h"

static unsigned int checks,failed;
static int legacy_peer;
ssize_t __real_recv(int,void *,size_t,int);
ssize_t __wrap_recv(int fd,void *buffer,size_t size,int flags)
{
    ssize_t n=__real_recv(fd,buffer,size,flags);
    /* Linux 2.6.10: unix_seqpacket_ops -> unix_dgram_recvmsg ->
     * skb_recv_datagram returns EAGAIN for an empty nonblocking socket,
     * even after peer shutdown. Real poll still supplies HUP|POLLIN. */
    if(legacy_peer&&n==0){errno=EAGAIN;return -1;}
    return n;
}
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
typedef struct fixture { const char *directory; int active_fd; } fixture_t;
static int stage(void *x,const gateway_network_settings_t *s)
{fixture_t*f=x;(void)s;return gateway_network_store_write(f->directory,"candidate","new",3);}
static int set_active(fixture_t*f,const char *s)
{return lseek(f->active_fd,0,SEEK_SET)<0||write(f->active_fd,s,3)!=3||fsync(f->active_fd)?-1:0;}
static int apply(void*x){return set_active(x,"new");}
static int ready(void*x){(void)x;return 1;}
static int commit(void*x){fixture_t*f=x;return gateway_network_store_confirm(f->directory);}
static int restore(void*x){fixture_t*f=x;char b[8];size_t n;if(gateway_network_store_boot(f->directory,b,sizeof(b),&n)||n!=3)return -1;return set_active(f,b);}
static int discard(void*x){fixture_t*f=x;return gateway_network_store_discard(f->directory);}
static const gateway_network_manager_ops_t ops={stage,apply,ready,commit,restore,discard};
static core_tick_t clock_ms(void*x)
{struct timespec t;(void)x;clock_gettime(CLOCK_MONOTONIC,&t);return (core_tick_t)t.tv_sec*1000U+(core_tick_t)(t.tv_nsec/1000000L);}
static void cleanup(const char *dir)
{const char *names[]={"active","confirmed","good","candidate","write.tmp","lock"};unsigned int i;char p[256];for(i=0;i<6U;++i){snprintf(p,sizeof(p),"%s/%s",dir,names[i]);unlink(p);}rmdir(dir);}
static int wait_state(int fd,gateway_network_state_t desired)
{
    unsigned int i;struct pollfd p;gateway_network_supervisor_status_t status;
    p.fd=fd;p.events=POLLIN;
    for(i=0;i<50U;++i){
        if(poll(&p,1,100)>0){ssize_t n=recv(fd,&status,sizeof(status),0);if(n<=0)return -1;if(n==(ssize_t)sizeof(status)&&status.state==desired)return 0;}
    }
    return -1;
}
static void scenario(int confirm,int kill_client)
{
    char dir[]="/tmp/4vrs-net-supervisor-XXXXXX",path[256],value[8];int fd[2],status,lock;
    pid_t child;fixture_t f;gateway_network_manager_t m;gateway_network_settings_t settings;size_t n;
    core_tick_t departure=0;struct rusage usage;
    CHECK(mkdtemp(dir)!=0);f.directory=dir;snprintf(path,sizeof(path),"%s/active",dir);
    f.active_fd=open(path,O_RDWR|O_CREAT|O_EXCL,0600);CHECK(f.active_fd>=0);
    CHECK(gateway_network_store_adopt(dir,"old",3)==0);CHECK(set_active(&f,"old")==0);
    CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET,0,fd)==0);
    gateway_network_settings_init(&settings);
    strcpy(settings.lan[0].address,"10.0.2.13");strcpy(settings.lan[0].netmask,"255.255.240.0");
    strcpy(settings.lan[1].address,"192.168.4.127");strcpy(settings.lan[1].netmask,"255.255.255.0");
    child=fork();CHECK(child>=0);
    if(child==0){
        int result;close(fd[0]);lock=gateway_network_store_lock(dir);if(lock<0)_exit(80);
        alarm(3); /* A failed peer-loss regression must not hang the suite. */
        if(gateway_network_manager_init(&m,&ops,&f))_exit(81);
        result=gateway_network_supervisor_run(fd[1],&m,&settings,clock_ms,0);
        gateway_network_store_unlock(lock);close(fd[1]);close(f.active_fd);_exit(result?82:0);
    }
    close(fd[1]);
    CHECK(wait_state(fd[0],GATEWAY_NETWORK_WAIT_BINDINGS)==0);
    CHECK(send(fd[0],"B",1,0)==1);CHECK(wait_state(fd[0],GATEWAY_NETWORK_WAIT_CONFIRM)==0);
    if(kill_client==2){
        CHECK(kill(child,SIGTERM)==0);
        CHECK(wait_state(fd[0],GATEWAY_NETWORK_REVERTED)==0);close(fd[0]);
    }else if(kill_client==3){
        departure=clock_ms(0);close(fd[0]);
    }else if(kill_client){
        /* Pass the only remaining client endpoint to a process which actually
         * dies by SIGKILL. The independent supervisor is not signalled. */
        int control[2];pid_t client;CHECK(pipe(control)==0);client=fork();CHECK(client>=0);
        if(client==0){char b;close(control[1]);(void)read(control[0],&b,1);kill(getpid(),SIGKILL);_exit(90);}
        close(control[0]);departure=clock_ms(0);close(fd[0]);CHECK(write(control[1],"x",1)==1);close(control[1]);
        CHECK(waitpid(client,&status,0)==client&&WIFSIGNALED(status));
    }else{
        CHECK(send(fd[0],confirm?"K":"R",1,0)==1);
        if(!confirm){CHECK(wait_state(fd[0],GATEWAY_NETWORK_ROLLBACK_BINDINGS)==0);CHECK(send(fd[0],"B",1,0)==1);}
        CHECK(wait_state(fd[0],confirm?GATEWAY_NETWORK_KEPT:GATEWAY_NETWORK_REVERTED)==0);close(fd[0]);
    }
    CHECK(wait4(child,&status,0,&usage)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    if(departure){
        unsigned long cpu=(unsigned long)(usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)*1000000UL+
            (unsigned long)usage.ru_utime.tv_usec+(unsigned long)usage.ru_stime.tv_usec;
        CHECK((core_tick_t)(clock_ms(0)-departure)<1000U);CHECK(cpu<500000UL);
        printf("peer loss legacy=%d latency_ms=%lu cpu_us=%lu\n",legacy_peer,
            (unsigned long)(clock_ms(0)-departure),cpu);
    }
    CHECK(lseek(f.active_fd,0,SEEK_SET)==0&&read(f.active_fd,value,3)==3);
    CHECK(!memcmp(value,confirm?"new":"old",3));
    CHECK(gateway_network_store_boot(dir,value,sizeof(value),&n)==0&&n==3&&!memcmp(value,confirm?"new":"old",3));
    close(f.active_fd);cleanup(dir);
}
int main(void)
{scenario(1,0);scenario(0,0);scenario(0,1);scenario(0,2);scenario(0,3);legacy_peer=1;scenario(0,1);scenario(0,3);printf("network supervisor checks=%u failed=%u\n",checks,failed);return failed?1:0;}
