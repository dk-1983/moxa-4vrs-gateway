#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "network/gateway_network_service.h"
#include "network/gateway_network_runtime.h"
#include "network/gateway_network_store.h"
#include "network/gateway_network_system.h"
#include "network/gateway_dhcp_io.h"
#define SERVICE_PROTOCOL 0x34564e34U
typedef struct command {unsigned int protocol;char code;unsigned int request;} command_t;
typedef struct mutation {
    char code;int result;
    gateway_network_observation_t target;
    char resolver[GATEWAY_NETWORK_FILE_MAX];
} mutation_t;
typedef struct service_context {
    const gateway_network_service_environment_t *environment;
    int guardian;gateway_dhcp_io_t io[2];
} service_context_t;
static volatile sig_atomic_t stopping;
static void stop_signal(int signal_number){(void)signal_number;stopping=1;}
static core_tick_t now_ms(void)
{struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))_exit(125);return (core_tick_t)((unsigned long)t.tv_sec*1000UL+(unsigned long)t.tv_nsec/1000000UL);}
static int path(char *out,size_t cap,const char *directory,const char *name)
{int n=snprintf(out,cap,"%s/%s",directory,name);return n<0||(size_t)n>=cap?-1:0;}
static int nonblock(int fd)
{int f=fcntl(fd,F_GETFL,0);return f<0||fcntl(fd,F_SETFL,f|O_NONBLOCK)<0||fcntl(fd,F_SETFD,FD_CLOEXEC)<0?-1:0;}
static int profile(const char *directory,const char *name,gateway_network_profile_t *p)
{
    char data[GATEWAY_NETWORK_SNAPSHOT_MAX];size_t size;gateway_network_store_result_t result;
    result=name?gateway_network_store_read(directory,name,data,sizeof(data),&size):gateway_network_store_boot(directory,data,sizeof(data),&size);
    return result==GATEWAY_NET_STORE_OK?gateway_network_profile_decode(data,size,p):-1;
}
static int real_observe(void *unused,gateway_network_observation_t *out)
{(void)unused;return gateway_network_observe(out);}
static int real_write(void *unused,const gateway_network_observation_t *out,const char *resolver)
{(void)unused;if(gateway_dhcp_foreign_client())return -1;return gateway_network_system_write(out,"/etc/resolv.conf",resolver);}
static const gateway_network_service_environment_t production={"/etc/4vrs-network",real_observe,real_write,0,0,0,0};
const gateway_network_service_environment_t *gateway_network_service_production(void){return &production;}
static int owner_observe(void *context,gateway_network_observation_t *out)
{service_context_t *c=context;return c->environment->observe(c->environment->context,out);}
static int owner_open(void *context,unsigned int lan)
{
    service_context_t *c=context;const gateway_network_owner_ops_t *ops=c->environment->client_ops;
    if(ops)return ops->open(c->environment->context,lan);
    if(gateway_dhcp_foreign_client())return -1;
    return gateway_dhcp_io_open(&c->io[lan],lan);
}
static void owner_close(void *context,unsigned int lan)
{service_context_t *c=context;if(c->environment->client_ops)c->environment->client_ops->close(c->environment->context,lan);else gateway_dhcp_io_close(&c->io[lan]);}
static int owner_receive(void *context,unsigned int lan,unsigned char *p,size_t n)
{service_context_t *c=context;return c->environment->client_ops?c->environment->client_ops->receive(c->environment->context,lan,p,n):gateway_dhcp_io_receive(&c->io[lan],p,n);}
static int owner_send(void *context,unsigned int lan,gateway_dhcp_client_t *d,core_tick_t now)
{service_context_t *c=context;return c->environment->client_ops?c->environment->client_ops->send(c->environment->context,lan,d,now):gateway_dhcp_io_send(&c->io[lan],d,now);}
static int owner_probe(void *context,unsigned int lan,const gateway_dhcp_client_t *d,unsigned int announce)
{service_context_t *c=context;return c->environment->client_ops?c->environment->client_ops->probe(c->environment->context,lan,d,announce):gateway_dhcp_io_probe(&c->io[lan],d,announce);}
static int owner_apply(void *context,const gateway_network_observation_t *target,const char *resolver)
{
    service_context_t *c=context;mutation_t message;memset(&message,0,sizeof(message));message.code='A';message.target=*target;
    if(strlen(resolver)>=sizeof(message.resolver))return -1;
    strcpy(message.resolver,resolver);
    return send(c->guardian,&message,sizeof(message),MSG_NOSIGNAL)==(ssize_t)sizeof(message)?0:-1;
}
static int owner_poll(void *context)
{
    service_context_t *c=context;mutation_t message;ssize_t n=recv(c->guardian,&message,sizeof(message),MSG_DONTWAIT|MSG_TRUNC);
    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    return n==(ssize_t)sizeof(message)&&message.code=='J'?message.result:-1;
}
static int owner_cancel(void *context)
{service_context_t *c=context;char command='X';return send(c->guardian,&command,1,MSG_NOSIGNAL)==1?0:-1;}
static const gateway_network_owner_ops_t owner_ops={owner_observe,owner_open,owner_close,owner_receive,owner_send,owner_probe,owner_apply,owner_poll,owner_cancel};
static void snapshot(const gateway_network_owner_t *owner,unsigned int request,gateway_network_service_status_t *status)
{
    unsigned int i;memset(status,0,sizeof(*status));status->protocol=SERVICE_PROTOCOL;status->ready=owner->ready;status->settled=owner->settled;status->error=owner->error;
    status->policy=owner->policy.settings;status->generation=owner->generation;status->request=request;status->observed=owner->observed;status->effective=owner->target;
    status->owner_pid=(int)getpid();status->guardian_pid=(int)getppid();
    for(i=0;i<2U;++i){status->dhcp_state[i]=(unsigned int)owner->dhcp[i].state;status->lease_valid[i]=owner->dhcp[i].valid;status->lease_expiry[i]=owner->dhcp[i].expires_at;}
}
static int listener(const char *directory)
{
    struct sockaddr_un address;struct stat st;int fd;
    memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;
    if(path(address.sun_path,sizeof(address.sun_path),directory,"owner.sock"))return -1;
    if(!lstat(address.sun_path,&st)){if(!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid()||unlink(address.sun_path))return -1;}
    else if(errno!=ENOENT)return -1;
    fd=socket(AF_UNIX,SOCK_SEQPACKET,0);if(fd<0)return -1;
    if(nonblock(fd)||bind(fd,(struct sockaddr *)&address,sizeof(address))||chmod(address.sun_path,0600)||listen(fd,4)){close(fd);return -1;}
    return fd;
}
static void run_owner(const gateway_network_service_environment_t *environment,int guardian,unsigned int bootstrap)
{
    gateway_network_owner_t owner;gateway_network_profile_t p;service_context_t context;
    gateway_network_service_status_t status,last;core_tick_t next_send=0,heartbeat=0,ownership_at=0;
    int server,client=-1;unsigned int request=0,request_error=0;struct sigaction action;
    memset(&action,0,sizeof(action));action.sa_handler=stop_signal;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,0);sigaction(SIGINT,&action,0);stopping=0;
    memset(&context,0,sizeof(context));context.environment=environment;context.guardian=guardian;
    context.io[0].packet=context.io[0].udp=context.io[1].packet=context.io[1].udp=-1;
    gateway_network_owner_init(&owner,&owner_ops,&context,(uint32_t)now_ms()^(uint32_t)getpid());
    if(bootstrap){if(profile(environment->directory,0,&p)||gateway_network_owner_policy(&owner,&p,environment->clock?environment->clock(environment->context):now_ms()))_exit(2);}
    server=listener(environment->directory);if(server<0)_exit(3);memset(&last,255,sizeof(last));
    heartbeat=next_send=now_ms();
    for(;;){struct pollfd fds[2];core_tick_t wall=now_ms(),now=environment->clock?environment->clock(environment->context):wall;int peer;
        if(stopping)_exit(0); /* Guardian performs confirmed recovery. */
        if((int32_t)(wall-heartbeat)>=0){char h='H';if(send(guardian,&h,1,MSG_NOSIGNAL)!=1)_exit(4);heartbeat=wall+250U;}
        if(!environment->client_ops&&(int32_t)(wall-ownership_at)>=0){
            ownership_at=wall+1000U;
            if(gateway_dhcp_foreign_client()){gateway_network_owner_stop(&owner);owner.error=11;}
        }
        gateway_network_owner_step(&owner,now);
        fds[0].fd=server;fds[0].events=POLLIN;fds[0].revents=0;
        fds[1].fd=client;fds[1].events=POLLIN;fds[1].revents=0;
        if(poll(fds,2,10)<0&&errno!=EINTR)_exit(5);
        if(client>=0){command_t command;ssize_t n=-1;
            if(fds[1].revents&(POLLHUP|POLLERR|POLLNVAL))n=0;
            else if(fds[1].revents&POLLIN)n=recv(client,&command,sizeof(command),MSG_DONTWAIT|MSG_TRUNC);
            if(n==0){
                close(client);client=-1;
                if(profile(environment->directory,0,&p)||gateway_network_owner_policy(&owner,&p,now))_exit(6);
            }else if(n==(ssize_t)sizeof(command)&&command.protocol==SERVICE_PROTOCOL){
                request=command.request;
                if(command.code=='A'||command.code=='R'||command.code=='K'){
                    request_error=profile(environment->directory,command.code=='A'?"candidate":0,&p)||gateway_network_owner_policy(&owner,&p,now)?10U:0U;
                }else if(command.code!='B'){close(client);client=-1;if(profile(environment->directory,0,&p)||gateway_network_owner_policy(&owner,&p,now))_exit(6);}
                next_send=wall;
            }else if(n>0){close(client);client=-1;if(profile(environment->directory,0,&p)||gateway_network_owner_policy(&owner,&p,now))_exit(6);}
        }
        if(fds[0].revents&POLLIN){
            peer=accept(server,0,0);
            if(peer>=0){struct ucred credentials;socklen_t size=sizeof(credentials);command_t command;ssize_t n;struct pollfd first;
                first.fd=peer;first.events=POLLIN;first.revents=0;
                if(nonblock(peer)||getsockopt(peer,SOL_SOCKET,SO_PEERCRED,&credentials,&size)||credentials.uid!=geteuid()||poll(&first,1,20)!=1){close(peer);continue;}
                n=recv(peer,&command,sizeof(command),MSG_DONTWAIT|MSG_TRUNC);
                if(n==(ssize_t)sizeof(command)&&command.protocol==SERVICE_PROTOCOL&&command.code=='Q'){
                    snapshot(&owner,command.request,&status);if(request_error){status.error=request_error;status.ready=status.settled=0;}(void)send(peer,&status,sizeof(status),MSG_NOSIGNAL);close(peer);
                }else if(n==(ssize_t)sizeof(command)&&command.protocol==SERVICE_PROTOCOL&&command.code=='B'&&client<0){client=peer;request=command.request;next_send=wall;memset(&last,255,sizeof(last));}
                else close(peer);
            }
        }
        if(client>=0){
            snapshot(&owner,request,&status);if(request_error){status.error=request_error;status.ready=status.settled=0;}
            if(memcmp(&status,&last,sizeof(status))||(int32_t)(wall-next_send)>=0){
                ssize_t n=send(client,&status,sizeof(status),MSG_NOSIGNAL);
                if(n==(ssize_t)sizeof(status)){last=status;next_send=wall+250U;}
            }
        }
    }
}
static int lifetime_lock(const char *directory)
{
    char name[256];int fd;struct flock lock;struct stat st;
    if(path(name,sizeof(name),directory,"owner.lock"))return -1;
    fd=open(name,O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(fd<0)return -1;
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||fcntl(fd,F_SETLK,&lock)){close(fd);return -1;}
    return fd;
}
static int recover_confirmed(const gateway_network_service_environment_t *e)
{
    gateway_network_profile_t p;gateway_network_observation_t desired;unsigned int i;char resolver[GATEWAY_NETWORK_FILE_MAX];size_t size;
    if(profile(e->directory,0,&p)||e->observe(e->context,&desired))return -1;
    for(i=0;i<2U;++i){
        if(p.settings.lan[i].mode==GATEWAY_LAN_STATIC){strcpy(desired.lan[i].address,p.settings.lan[i].address);strcpy(desired.lan[i].netmask,p.settings.lan[i].netmask);
            if(gateway_network_profile_broadcast(&p,i,desired.lan[i].broadcast))return -1;
        }else{memset(desired.lan[i].address,0,16);memset(desired.lan[i].netmask,0,16);memset(desired.lan[i].broadcast,0,16);}
    }
    desired.default_lan=desired.default_routes=0;memset(desired.gateway,0,16);
    i=p.settings.default_lan;
    if(i&&p.settings.lan[i-1U].mode==GATEWAY_LAN_STATIC){desired.default_lan=i;desired.default_routes=1;strcpy(desired.gateway,p.settings.lan[i-1U].gateway);}
    memcpy(desired.dns,p.settings.dns,sizeof(desired.dns));strcpy(resolver,p.resolver);
    if(p.settings.automatic_dns){memset(desired.dns,0,sizeof(desired.dns));if(gateway_network_resolver(p.resolver,strlen(p.resolver),(const char (*)[16])desired.dns,resolver,sizeof(resolver),&size))return -1;}
    return e->write(e->context,&desired,resolver);
}
static void guardian(const gateway_network_service_environment_t *e)
{
    int lock,fd=-1,pair[2],status,stopped=0,recovering=0;pid_t owner=0,job=0,pid;core_tick_t heartbeat=0,deadline=0;
    unsigned int bootstrap=0;mutation_t message;struct sigaction action;
    if(gateway_network_process_isolate(-1))_exit(10);
    memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;sigemptyset(&action.sa_mask);
    if(sigaction(SIGCHLD,&action,0))_exit(10);
    lock=lifetime_lock(e->directory);if(lock<0)_exit(11);
    memset(&action,0,sizeof(action));action.sa_handler=stop_signal;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,0);sigaction(SIGINT,&action,0);stopping=0;
    for(;;){core_tick_t now=now_ms();struct pollfd p;ssize_t n;
        if(!owner&&!job&&!recovering&&!stopping){
            if(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair)||nonblock(pair[0])||nonblock(pair[1]))_exit(12);
            {int bytes=65536;unsigned int i;for(i=0;i<2U;++i)
                if(setsockopt(pair[i],SOL_SOCKET,SO_SNDBUF,&bytes,sizeof(bytes))||setsockopt(pair[i],SOL_SOCKET,SO_RCVBUF,&bytes,sizeof(bytes)))_exit(12);}
            owner=fork();if(owner<0)_exit(13);
            if(!owner){close(pair[0]);if(gateway_network_process_isolate(pair[1]))_exit(14);run_owner(e,pair[1],bootstrap);_exit(15);}
            close(pair[1]);fd=pair[0];heartbeat=now;
        }
        p.fd=stopped?-1:fd;p.events=POLLIN;p.revents=0;
        if(poll(&p,1,10)<0&&errno!=EINTR)stopping=1;
        if(owner&&!stopped&&(stopping||(p.revents&(POLLHUP|POLLERR|POLLNVAL))||(int32_t)(now-heartbeat)>=2000)){
            (void)kill(owner,SIGKILL);if(job)(void)kill(job,SIGKILL);stopped=1;
        }
        if(fd>=0&&(p.revents&POLLIN)&&!stopped){
            n=recv(fd,&message,sizeof(message),MSG_DONTWAIT|MSG_TRUNC);
            if(n==1&&message.code=='H')heartbeat=now;
            else if(n==1&&message.code=='X'){if(job)(void)kill(job,SIGKILL);}
            else if(n==(ssize_t)sizeof(message)&&message.code=='A'&&!job){
                job=fork();if(job<0)_exit(16);
                if(!job){if(gateway_network_process_isolate(-1))_exit(17);_exit(e->write(e->context,&message.target,message.resolver)?1:0);}
                deadline=now+1000U;
            }
        }
        if(job){
            if((int32_t)(now-deadline)>=0)(void)kill(job,SIGKILL);
            pid=waitpid(job,&status,WNOHANG);
            if(pid==job){job=0;
                if(recovering){if(!WIFEXITED(status)||WEXITSTATUS(status))_exit(18);recovering=0;bootstrap=1;}
                else if(fd>=0&&!stopped){memset(&message,0,sizeof(message));message.code='J';message.result=WIFEXITED(status)&&!WEXITSTATUS(status)?1:-1;
                    if(send(fd,&message,sizeof(message),MSG_NOSIGNAL)!=(ssize_t)sizeof(message))stopped=1;}
            }
        }
        if(stopped&&owner){(void)kill(owner,SIGKILL);if(job)(void)kill(job,SIGKILL);}
        if(owner){pid=waitpid(owner,&status,WNOHANG);if(pid==owner){owner=0;stopped=1;if(fd>=0)close(fd);fd=-1;if(job)(void)kill(job,SIGKILL);}}
        if(stopped&&!owner&&!job&&!recovering){
            job=fork();if(job<0)_exit(19);
            if(!job){if(gateway_network_process_isolate(-1))_exit(20);_exit(recover_confirmed(e)?1:0);}
            recovering=1;stopped=0;deadline=now+30000U;
        }
        if(stopping&&!owner&&!job&&!recovering){close(lock);_exit(0);}
    }
}
static int connect_socket(const char *directory)
{
    struct sockaddr_un address;int fd;struct ucred credentials;socklen_t size=sizeof(credentials);
    memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;
    if(path(address.sun_path,sizeof(address.sun_path),directory,"owner.sock"))return -1;
    fd=socket(AF_UNIX,SOCK_SEQPACKET,0);if(fd<0)return -1;
    if(nonblock(fd)||connect(fd,(struct sockaddr *)&address,sizeof(address))){close(fd);return -1;}
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credentials,&size)||credentials.uid!=geteuid()){close(fd);return -1;}
    return fd;
}
int gateway_network_service_command(int fd,char code,unsigned int request)
{command_t command;memset(&command,0,sizeof(command));command.protocol=SERVICE_PROTOCOL;command.code=code;command.request=request;return send(fd,&command,sizeof(command),MSG_NOSIGNAL)==(ssize_t)sizeof(command)?0:-1;}
int gateway_network_service_read(int fd,gateway_network_service_status_t *status)
{ssize_t n=recv(fd,status,sizeof(*status),MSG_DONTWAIT|MSG_TRUNC);if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;return n==(ssize_t)sizeof(*status)&&status->protocol==SERVICE_PROTOCOL?1:-1;}
int gateway_network_service_connect(const gateway_network_service_environment_t *e,unsigned int start)
{
    int fd;unsigned int attempt;pid_t pid;
    if(!e||!e->directory||!e->observe||!e->write)return -1;
    fd=connect_socket(e->directory);
    if(fd<0&&start){pid=fork();if(pid<0)return -1;if(!pid){guardian(e);_exit(21);}
        if(e->started_guardian)*e->started_guardian=(int)pid;
        for(attempt=0;attempt<100U&&fd<0;++attempt){usleep(10000);fd=connect_socket(e->directory);}
        /* Guardian intentionally outlives the caller. No wait on its lifetime. */
    }
    if(fd>=0&&gateway_network_service_command(fd,'B',0)){close(fd);return -1;}
    return fd;
}
int gateway_network_service_query(const gateway_network_service_environment_t *e,gateway_network_service_status_t *out)
{
    int fd=connect_socket(e->directory),result;struct pollfd p;
    if(fd<0)return -1;
    if(gateway_network_service_command(fd,'Q',1)){close(fd);return -1;}
    p.fd=fd;p.events=POLLIN;p.revents=0;result=poll(&p,1,100);
    result=result==1?gateway_network_service_read(fd,out):-1;close(fd);return result==1?0:-1;
}
