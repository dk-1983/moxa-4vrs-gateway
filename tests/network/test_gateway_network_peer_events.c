#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "network/gateway_network_supervisor.h"

/* Deterministic syscall boundary faults. No host network or time mutation.
 * In particular HUP + EAGAIN models upstream Linux 2.6.10 seqpacket shutdown.
 * The real-process companion test measures SIGKILL latency and CPU usage. */
static unsigned int checks,failed;
#define CHECK(x) do {++checks;if(!(x)){++failed;printf("FAIL %d %s\n",__LINE__,#x);}} while(0)
static gateway_network_manager_t manager;
static gateway_network_state_t target;
static unsigned int mode,lost,detected,repolls,reads_after_loss,records,pending,restores,commits,writes;
static core_tick_t now,lost_at,restore_at;
static unsigned long final_loops;
enum {HUP,ERR,NVAL,EOF_EVENT,RECV_ERROR,SEND_ERROR,POLL_ERROR,MODES};
static int trigger(void)
{
    if(!lost&&manager.state==target){lost=1;lost_at=now;return 1;}
    return 0;
}
int __wrap_poll(struct pollfd *fds,nfds_t n,int timeout)
{
    CHECK(n==1);fds[0].revents=0;
    if(fds[0].fd<0){now+=(core_tick_t)timeout;return 0;}
    if(detected)++repolls;
    if(mode!=SEND_ERROR)(void)trigger();
    if(lost){
        if(mode==POLL_ERROR){detected=1;errno=EBADF;return -1;}
        if(mode==HUP||mode==ERR||mode==NVAL)detected=1;
        fds[0].revents=mode==HUP?(POLLHUP|POLLIN):mode==ERR?POLLERR:mode==NVAL?POLLNVAL:POLLIN;
        return 1;
    }
    now+=(core_tick_t)timeout;return 0;
}
ssize_t __wrap_recv(int fd,void *bytes,size_t n,int flags)
{
    char command=0;(void)fd;(void)n;(void)flags;
    if(lost){
        ++reads_after_loss;
        if(mode==EOF_EVENT){detected=1;return 0;}
        if(mode==RECV_ERROR){detected=1;errno=ECONNRESET;return -1;}
        /* A queued Keep must never override terminal poll flags. */
        if(mode==HUP){*(char *)bytes='K';return 1;}
        errno=EAGAIN;return -1;
    }
    if(manager.state==GATEWAY_NETWORK_WAIT_BINDINGS)command='B';
    if(manager.state==GATEWAY_NETWORK_WAIT_CONFIRM)
        command=target==GATEWAY_NETWORK_COMMITTING?'K':'R';
    if(command){*(char *)bytes=command;return 1;}
    errno=EAGAIN;return -1;
}
ssize_t __wrap_send(int fd,const void *bytes,size_t n,int flags)
{
    (void)fd;(void)bytes;(void)flags;
    if(mode==SEND_ERROR&&(trigger()||lost)){detected=1;errno=EPIPE;return -1;}
    return (ssize_t)n;
}
static int stage(void *x,const gateway_network_settings_t *s){(void)x;(void)s;return 0;}
static int apply(void *x){(void)x;++writes;pending=2;return 0;}
static int ready(void *x){(void)x;if(pending){--pending;return 0;}return 1;}
static int commit(void *x){(void)x;++commits;return 0;}
static int restore(void *x){(void)x;++restores;++writes;restore_at=now;pending=5;return 0;}
static int discard(void *x){(void)x;return 0;}
static const gateway_network_manager_ops_t ops={stage,apply,ready,commit,restore,discard};
static core_tick_t clock_ms(void *x){(void)x;return now;}
static void trace(void *x,core_tick_t stamp,const gateway_network_manager_t *m,
    gateway_network_supervisor_event_t event,int detail,unsigned long loops)
{
    (void)x;(void)m;(void)detail;CHECK(stamp==now);++records;
    if(event==GATEWAY_NET_EVENT_FINISH)final_loops=loops;
}
static void scenario(gateway_network_state_t state,unsigned int fault)
{
    int pair[2];gateway_network_settings_t s;
    target=state;mode=fault;lost=detected=repolls=reads_after_loss=records=pending=restores=commits=writes=0;
    now=lost_at=restore_at=0;final_loops=0;
    CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair)==0);
    gateway_network_settings_init(&s);
    strcpy(s.lan[0].address,"10.0.2.13");strcpy(s.lan[0].netmask,"255.255.240.0");
    strcpy(s.lan[1].address,"192.168.4.127");strcpy(s.lan[1].netmask,"255.255.255.0");
    CHECK(gateway_network_manager_init(&manager,&ops,0)==0);
    alarm(3);
    CHECK(gateway_network_supervisor_run_traced(pair[0],&manager,&s,clock_ms,0,trace,0)==0);
    alarm(0);
    CHECK(lost);CHECK(!repolls);CHECK(!commits);CHECK(restores==1);CHECK(writes==2);
    CHECK(manager.state==GATEWAY_NETWORK_REVERTED);CHECK(!manager.client_alive);
    CHECK(records>0&&records<=GATEWAY_NETWORK_TRACE_LIMIT);CHECK(final_loops<20UL);
    if(state<GATEWAY_NETWORK_ROLLING_BACK){
        CHECK((core_tick_t)(restore_at-lost_at)<=10U);
        CHECK(manager.rollback_reason==GATEWAY_NETWORK_REASON_PEER);
    }else CHECK(manager.rollback_reason==GATEWAY_NETWORK_REASON_REVERT);
    CHECK(reads_after_loss==((mode==EOF_EVENT||mode==RECV_ERROR)?1U:0U));
    close(pair[0]);close(pair[1]);
}
int main(void)
{
    unsigned int s,f;
    for(s=GATEWAY_NETWORK_APPLYING;s<=GATEWAY_NETWORK_ROLLBACK_BINDINGS;++s)
        for(f=0;f<MODES;++f)scenario((gateway_network_state_t)s,f);
    printf("supervisor peer events: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
