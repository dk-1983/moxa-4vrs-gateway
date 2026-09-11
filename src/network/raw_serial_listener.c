#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "network/raw_serial_listener.h"

static void count(unsigned int *v, unsigned int n)
{ *v = 0xffffffffU - *v < n ? 0xffffffffU : *v + n; }
static int transient(int e) { return e==EAGAIN || e==EWOULDBLOCK || e==EINTR; }
static int nonblock(int fd)
{ int f=fcntl(fd,F_GETFL,0); return f<0?-1:fcntl(fd,F_SETFL,f|O_NONBLOCK); }

static void serial_fault(raw_serial_listener_t *l)
{
    count(&l->stats.io_errors,1);
    port_runtime_report_io_failure(l->port,l->now,"raw serial I/O failed");
}

static int flush_boundary(raw_serial_listener_t *l)
{
    if(!l->flush_pending)return 0;
    if(l->uart->fd<0)return -1;
    if(l->uart->sys->tcflush_fn(l->uart->sys_context,l->uart->fd,TCIOFLUSH)!=0){
        serial_fault(l);return -1;
    }
    l->flush_pending=0;l->guard=1;l->quiet_at=l->now;
    return 0;
}

/* Never retain bytes from a previous network owner. UART lifecycle remains
 * owned by the backend; flushing here only fences a RAW session boundary. */
static int release(raw_serial_listener_t *l)
{
    int result=0;
    if(l->client_fd>=0) close(l->client_fd);
    l->client_fd=-1;
    if(l->owned) {
        count(&l->stats.releases,1);
        l->flush_pending=1;
    }
    result=flush_boundary(l);
    l->owned=l->eof=0; l->tx_length=l->rx_length=0;
    l->guard=1; l->quiet_at=l->now;
    return result;
}

void raw_serial_listener_init(raw_serial_listener_t *l,port_runtime_t *p,uart_backend_t *u,int udp)
{
    memset(l,0,sizeof(*l)); l->fd=l->client_fd=-1; l->port=p;l->uart=u;l->udp=udp!=0;
    port_runtime_set_completion(p,0,0);
}

int raw_serial_listener_open(raw_serial_listener_t *l,const char *address,unsigned short port)
{
    struct sockaddr_in sa; int fd,yes=1;
    if(!l||!address||!l->uart||!l->port||l->fd>=0) return -1;
    memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_port=htons(port);
    sa.sin_addr.s_addr=inet_addr(address);if(sa.sin_addr.s_addr==INADDR_NONE)return -1;
    fd=socket(AF_INET,l->udp?SOCK_DGRAM:SOCK_STREAM,0);if(fd<0)return -1;
    if(!l->udp)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    if(nonblock(fd)<0||bind(fd,(struct sockaddr*)&sa,sizeof(sa))<0||(!l->udp&&listen(fd,4)<0)) {
        close(fd);return -1;
    }
    l->fd=fd;return 0;
}

static void acquire(raw_serial_listener_t *l)
{ l->owned=1;l->eof=0;l->activity_at=l->tx_progress_at=l->rx_progress_at=l->now; }

static void network_receive(raw_serial_listener_t *l)
{
    unsigned int i; ssize_t n;
    if(l->udp) {
        for(i=0;i<RAW_NETWORK_BUDGET;++i) {
            unsigned char data[RAW_BUFFER_SIZE+1U];struct sockaddr_in from;socklen_t size=sizeof(from);
            n=recvfrom(l->fd,data,sizeof(data),0,(struct sockaddr*)&from,&size);
            if(n<0){if(!transient(errno))count(&l->stats.io_errors,1);break;}
            if(n==0||n>(ssize_t)RAW_BUFFER_SIZE||size!=sizeof(from)||from.sin_family!=AF_INET) {
                count(&l->stats.dropped_datagrams,1);continue;
            }
            if(l->guard||(l->owned&&(from.sin_addr.s_addr!=l->peer.sin_addr.s_addr||from.sin_port!=l->peer.sin_port))) {
                count(&l->stats.rejected_peers,1);continue;
            }
            if((unsigned int)n>RAW_BUFFER_SIZE-l->tx_length) {count(&l->stats.dropped_datagrams,1);continue;}
            if(!l->owned){l->peer=from;acquire(l);}
            if(!l->tx_length)l->tx_progress_at=l->now;
            memcpy(l->tx+l->tx_length,data,(size_t)n);l->tx_length+=(unsigned int)n;
            l->activity_at=l->now;count(&l->stats.network_received,(unsigned int)n);
        }
    } else {
        for(i=0;i<RAW_NETWORK_BUDGET;++i) {
            int fd=accept(l->fd,0,0);if(fd<0)break;
            if(l->owned||l->guard||nonblock(fd)<0){close(fd);count(&l->stats.rejected_peers,1);continue;}
            {int yes=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));}
            l->client_fd=fd;acquire(l);
        }
        if(!l->owned||l->eof||l->tx_length==RAW_BUFFER_SIZE)return;
        n=recv(l->client_fd,l->tx+l->tx_length,RAW_BUFFER_SIZE-l->tx_length,0);
        if(n>0){if(!l->tx_length)l->tx_progress_at=l->now;l->tx_length+=(unsigned int)n;
            l->activity_at=l->now;count(&l->stats.network_received,(unsigned int)n);}
        else if(n==0){l->eof=1;l->activity_at=l->now;}
        else if(!transient(errno)){count(&l->stats.io_errors,1);release(l);}
    }
}

static void serial_read(raw_serial_listener_t *l)
{
    unsigned char data[UART_IO_BUDGET];unsigned int capacity;
    ssize_t n;uart_backend_t *u=l->uart;
    capacity=l->owned?RAW_BUFFER_SIZE-l->rx_length:UART_IO_BUDGET;
    if(capacity>UART_IO_BUDGET)capacity=UART_IO_BUDGET;
    if(!capacity)return;
    count(&u->stats.read_calls,1);
    n=u->sys->read_fn(u->sys_context,u->fd,data,capacity);
    if(n>0){
        count(&l->stats.serial_read,(unsigned int)n);count(&u->stats.bytes_read,(unsigned int)n);
        if(!l->owned){count(&l->stats.discarded_serial,(unsigned int)n);l->guard=1;l->quiet_at=l->now;return;}
        if(!l->rx_length)l->rx_progress_at=l->now;
        memcpy(l->rx+l->rx_length,data,(size_t)n);l->rx_length+=(unsigned int)n;
        l->activity_at=l->serial_at=l->now;
    }else if(n<0&&!transient(u->sys->last_error_fn(u->sys_context))){count(&u->stats.hard_errors,1);release(l);serial_fault(l);}
    else if(n<0&&u->sys->last_error_fn(u->sys_context)==EINTR){if(!l->owned){l->guard=1;l->quiet_at=l->now;}}
    else if(!l->owned&&l->guard&&core_elapsed(l->quiet_at,l->now)>=RAW_OWNER_GUARD_MS)l->guard=0;
}

static void serial_write(raw_serial_listener_t *l)
{
    unsigned int length=l->tx_length;ssize_t n;uart_backend_t *u=l->uart;
    if(!length)return;
    if(length>UART_IO_BUDGET)length=UART_IO_BUDGET;
    count(&u->stats.write_calls,1);
    n=u->sys->write_fn(u->sys_context,u->fd,l->tx,length);
    if(n>0){
        if((unsigned int)n<length)count(&u->stats.short_writes,1);
        l->tx_length-=(unsigned int)n;memmove(l->tx,l->tx+n,l->tx_length);
        l->tx_progress_at=l->activity_at=l->now;
        count(&l->stats.serial_written,(unsigned int)n);count(&u->stats.bytes_written,(unsigned int)n);
    }else if(n<0&&!transient(u->sys->last_error_fn(u->sys_context))){count(&u->stats.hard_errors,1);release(l);serial_fault(l);}
}

static void network_send(raw_serial_listener_t *l)
{
    ssize_t n;
    if(!l->owned||!l->rx_length)return;
    if(l->udp){
        if(l->rx_length<RAW_BUFFER_SIZE&&core_elapsed(l->serial_at,l->now)<RAW_PACKET_GAP_MS)return;
        n=sendto(l->fd,l->rx,l->rx_length,0,(struct sockaddr*)&l->peer,sizeof(l->peer));
        if(n>=0&&n!=(ssize_t)l->rx_length){count(&l->stats.io_errors,1);release(l);return;}
    }else n=send(l->client_fd,l->rx,l->rx_length,MSG_NOSIGNAL);
    if(n>0){l->rx_length-=(unsigned int)n;memmove(l->rx,l->rx+n,l->rx_length);
        l->rx_progress_at=l->activity_at=l->now;count(&l->stats.network_sent,(unsigned int)n);}
    else if(n<0&&!transient(errno)){count(&l->stats.io_errors,1);release(l);}
}

void raw_serial_listener_step(raw_serial_listener_t *l,core_tick_t now)
{
    if(!l||l->fd<0||l->uart->fd<0||l->port->state!=PORT_READY||l->port->has_active||l->port->queue.count)return;
    l->now=now;
    /* Recovery drains input only. Retry a failed output flush before admitting
     * another owner, then observe a fresh quiet interval. */
    if(flush_boundary(l)!=0)return;
    if(l->owned&&((l->udp&&core_elapsed(l->activity_at,now)>=RAW_UDP_IDLE_MS)||
        (l->eof&&!l->tx_length&&!l->rx_length&&core_elapsed(l->activity_at,now)>=RAW_EOF_IDLE_MS)))release(l);
    if(l->owned&&((l->tx_length&&core_elapsed(l->tx_progress_at,now)>=RAW_STALL_MS)||
        (l->rx_length&&core_elapsed(l->rx_progress_at,now)>=RAW_STALL_MS))){count(&l->stats.stalls,1);release(l);}
    if(l->port->state!=PORT_READY)return;
    /* Drain before assigning a new owner; stale bytes cannot be adopted. */
    serial_read(l);if(l->port->state!=PORT_READY)return;
    network_receive(l);if(l->port->state!=PORT_READY)return;
    serial_write(l);if(l->port->state!=PORT_READY)return;
    network_send(l);
}

int raw_serial_listener_close(raw_serial_listener_t *l)
{
    int result;if(!l)return -1;
    result=release(l);if(l->fd>=0)close(l->fd);l->fd=-1;return result;
}
