#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "network/gateway_network_supervisor.h"

static volatile sig_atomic_t stopping;
static void request_stop(int signal_number){stopping=signal_number;}

int gateway_network_supervisor_run_traced(int fd,gateway_network_manager_t *m,
                                   const gateway_network_settings_t *candidate,
                                   gateway_network_supervisor_clock_fn clock,void *ctx,
                                   gateway_network_supervisor_trace_fn trace,void *trace_ctx)
{
    struct sigaction action;struct pollfd p;
    gateway_network_supervisor_status_t status;
    gateway_network_state_t announced=GATEWAY_NETWORK_UNAVAILABLE;
    int flags,channel_alive=1;unsigned int records=0;unsigned long iterations=0;
    gateway_network_state_t observed=GATEWAY_NETWORK_UNAVAILABLE;
#define TRACE(event,detail) do { if(trace&&records<GATEWAY_NETWORK_TRACE_LIMIT){ \
    ++records;trace(trace_ctx,clock(ctx),m,event,detail,iterations); } } while(0)
#define LOST(event,detail) do { if(channel_alive){channel_alive=0;m->client_alive=0; \
    TRACE(event,detail); } } while(0)
    if(fd<0||!m||!clock||!candidate||m->state!=GATEWAY_NETWORK_IDLE)return -1;
    flags=fcntl(fd,F_GETFL,0);
    if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return -1;
    memset(&action,0,sizeof(action));sigemptyset(&action.sa_mask);action.sa_handler=SIG_IGN;
    if(sigaction(SIGPIPE,&action,0))return -1;
    stopping=0;action.sa_handler=request_stop;
    if(sigaction(SIGTERM,&action,0)||sigaction(SIGINT,&action,0)||sigaction(SIGHUP,&action,0))return -1;
    /* Recovery's process, channel and signal handling are established before
     * the first provider mutation. A partial apply must enter rollback here. */
    (void)gateway_network_manager_apply(m,candidate,clock(ctx));
    for(;;){
        char command;ssize_t n;int r;
        ++iterations;
        /* Terminal poll events take precedence over queued Keep. Linux 2.6.10
         * seqpacket recv may return EAGAIN, rather than EOF, after shutdown.
         * Never poll or read a failed endpoint again: HUP is level triggered. */
        p.fd=channel_alive?fd:-1;p.events=POLLIN;p.revents=0;
        r=poll(&p,1,0);
        if(r<0&&errno!=EINTR){int error=errno;LOST(GATEWAY_NET_EVENT_POLL_ERROR,error);}
        else if(p.revents&(POLLHUP|POLLERR|POLLNVAL))LOST(GATEWAY_NET_EVENT_HUP,p.revents);
        n=-1;errno=EAGAIN;
        if(channel_alive&&m->client_alive)n=recv(fd,&command,1,0);
        if(n==0)LOST(GATEWAY_NET_EVENT_EOF,0);
        else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){int error=errno;LOST(GATEWAY_NET_EVENT_RECV_ERROR,error);}
        else if(n==1){
            if(command=='K')(void)gateway_network_manager_confirm(m);
            else if(command=='R')gateway_network_manager_revert(m);
            else if(command=='D'){m->client_alive=0;TRACE(GATEWAY_NET_EVENT_DEPART,0);}
            else if(command=='B'){
                if(m->state==GATEWAY_NETWORK_WAIT_BINDINGS||m->state==GATEWAY_NETWORK_ROLLBACK_BINDINGS)m->bindings_ready=1;
            }
            else gateway_network_manager_revert(m);
        }
        if(stopping&&m->client_alive){m->client_alive=0;TRACE(GATEWAY_NET_EVENT_SIGNAL,(int)stopping);gateway_network_manager_revert(m);}
        if(m->state!=observed){TRACE(GATEWAY_NET_EVENT_STATE,0);observed=m->state;}
        gateway_network_manager_step(m,clock(ctx));
        if(m->state!=observed){TRACE(GATEWAY_NET_EVENT_STATE,0);observed=m->state;}
        if(channel_alive&&m->state!=announced){
            status.state=m->state;status.deadline=m->deadline.at;
            status.rollback_reason=m->rollback_reason;status.error_code=m->error_code;
            n=send(fd,&status,sizeof(status),0);
            if(n==(ssize_t)sizeof(status))announced=m->state;
            else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){int error=errno;LOST(GATEWAY_NET_EVENT_SEND_ERROR,error);}
        }
        if(!gateway_network_manager_busy(m))break;
        p.fd=m->client_alive?fd:-1;p.events=POLLIN;p.revents=0;
        r=poll(&p,1,10);
        if(r<0&&errno!=EINTR){int error=errno;LOST(GATEWAY_NET_EVENT_POLL_ERROR,error);}
        else if(p.revents&(POLLHUP|POLLERR|POLLNVAL))LOST(GATEWAY_NET_EVENT_HUP,p.revents);
    }
    TRACE(GATEWAY_NET_EVENT_FINISH,0);
#undef LOST
#undef TRACE
    return m->state==GATEWAY_NETWORK_KEPT||m->state==GATEWAY_NETWORK_REVERTED?0:-1;
}

int gateway_network_supervisor_run(int fd,gateway_network_manager_t *m,
    const gateway_network_settings_t *candidate,gateway_network_supervisor_clock_fn clock,void *ctx)
{return gateway_network_supervisor_run_traced(fd,m,candidate,clock,ctx,0,0);}
