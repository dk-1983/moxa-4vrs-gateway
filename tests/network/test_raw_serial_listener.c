#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "network/raw_serial_listener.h"

static unsigned int checks,failures;
#define CHECK(x) do {++checks;if(!(x)){++failures;printf("FAIL %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
typedef struct fake {
    unsigned char bytes[8192],written[8192];
    unsigned int length,total,flushes;
    int error,blocked,hard,echo,flush_hard;
} fake_t;
typedef struct fixture {
    fake_t fake;uart_backend_t uart;port_runtime_t port;raw_serial_listener_t raw;
    struct sockaddr_in address;core_tick_t now;int client;
} fixture_t;
static ssize_t rd(void*c,int fd,void*b,size_t n)
{
    fake_t*f=c;(void)fd;
    if(f->hard){f->error=EIO;return -1;}
    if(!f->length){f->error=EAGAIN;return -1;}
    if(n>7)n=7;
    if(n>f->length)n=f->length;
    memcpy(b,f->bytes,n);f->length-=(unsigned)n;memmove(f->bytes,f->bytes+n,f->length);return (ssize_t)n;
}
static ssize_t wr(void*c,int fd,const void*b,size_t n)
{
    fake_t*f=c;(void)fd;
    if(f->blocked){f->error=EAGAIN;return -1;}
    if(n>3)n=3;
    CHECK(f->total+n<=sizeof(f->written));memcpy(f->written+f->total,b,n);f->total+=(unsigned)n;
    if(f->echo){CHECK(f->length+n<=sizeof(f->bytes));memcpy(f->bytes+f->length,b,n);f->length+=(unsigned)n;}
    return (ssize_t)n;
}
static int flush(void*c,int fd,int selector){fake_t*f=c;(void)fd;CHECK(selector==TCIOFLUSH);++f->flushes;if(f->flush_hard){f->error=EIO;return -1;}f->length=0;return 0;}
static int error(void*c){return ((fake_t*)c)->error;}
static int poll_backend(void*c,core_tick_t t,backend_event_t*e){(void)c;(void)t;(void)e;return 0;}
static int recover(void*c,core_tick_t t){(void)c;(void)t;return 0;}
static int start_backend(void*c,const core_transaction_t*x,core_tick_t t)
{(void)c;(void)x;(void)t;CHECK(0);return -1;}
static void cancel_backend(void*c,unsigned int g){(void)c;(void)g;CHECK(0);}
static int configure_backend(void*c,const core_port_config_t*x,core_tick_t t)
{(void)c;(void)x;(void)t;return 0;}
static const core_backend_ops_t backend={recover,start_backend,poll_backend,cancel_backend,recover,configure_backend,recover};
static const uart_syscalls_t sys={0,0,rd,wr,0,0,0,flush,error};
static void pump(fixture_t*f,unsigned n){while(n--){raw_serial_listener_step(&f->raw,++f->now);port_runtime_step(&f->port,f->now);}}
static int client(fixture_t*f,int udp)
{
    int s=socket(AF_INET,udp?SOCK_DGRAM:SOCK_STREAM,0),yes=1;CHECK(s>=0);
    if(!udp)setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    CHECK(connect(s,(struct sockaddr*)&f->address,sizeof(f->address))==0);return s;
}
static void init(fixture_t*f,int udp)
{
    socklen_t n=sizeof(f->address);memset(f,0,sizeof(*f));
    f->fake.echo=1;f->uart.fd=123;f->uart.sys=&sys;f->uart.sys_context=&f->fake;
    f->port.state=PORT_READY;f->port.backend=&backend;f->port.recovery_delay=5;
    raw_serial_listener_init(&f->raw,&f->port,&f->uart,udp);
    CHECK(raw_serial_listener_open(&f->raw,"127.0.0.1",0)==0);
    CHECK(getsockname(f->raw.fd,(struct sockaddr*)&f->address,&n)==0);
    f->client=client(f,udp);
}
static void finish(fixture_t*f){CHECK(raw_serial_listener_close(&f->raw)==0);close(f->client);}
static unsigned read_all(int s,unsigned char*b,unsigned cap)
{
    unsigned total=0;ssize_t n;
    while(total<cap&&(n=recv(s,b+total,cap-total,MSG_DONTWAIT))>0)total+=(unsigned)n;
    return total;
}
static void bytes_unchanged(int udp)
{
    fixture_t f;unsigned char data[256],answer[1024];unsigned i,n;int other;
    init(&f,udp);for(i=0;i<sizeof(data);++i)data[i]=(unsigned char)i;
    CHECK(send(f.client,data,sizeof(data),0)==(ssize_t)sizeof(data));pump(&f,400);
    n=read_all(f.client,answer,sizeof(answer));CHECK(n==sizeof(data));CHECK(!memcmp(data,answer,sizeof(data)));
    CHECK(f.fake.total==sizeof(data));CHECK(!memcmp(data,f.fake.written,sizeof(data)));
    CHECK(f.raw.stats.serial_read==sizeof(data)&&f.raw.stats.serial_written==sizeof(data));
    CHECK(f.port.stats.accepted==0); /* RAW does not invoke a Modbus transaction. */
    other=client(&f,udp);if(udp)CHECK(send(other,"foreign",7,0)==7);
    pump(&f,10);CHECK(f.raw.stats.rejected_peers==1);CHECK(f.fake.total==sizeof(data));close(other);
    /* Unsolicited serial bytes go to the current owner. */
    memcpy(f.fake.bytes,"unsolicited",11);f.fake.length=11;pump(&f,30);
    n=read_all(f.client,answer,sizeof(answer));CHECK(n==11);CHECK(!memcmp(answer,"unsolicited",11));
    finish(&f);CHECK(f.fake.flushes==1);CHECK(f.raw.fd==-1&&f.raw.client_fd==-1);
}
static void udp_limits_and_expiry(void)
{
    fixture_t f;unsigned char data[RAW_BUFFER_SIZE+1];int other;unsigned total;
    init(&f,1);memset(data,0x55,sizeof(data));f.fake.blocked=1;
    CHECK(send(f.client,data,0,0)==0);CHECK(send(f.client,data,sizeof(data),0)==(ssize_t)sizeof(data));pump(&f,1);
    CHECK(f.raw.stats.dropped_datagrams==2&&!f.raw.owned&&f.fake.total==0);
    CHECK(send(f.client,data,RAW_BUFFER_SIZE,0)==RAW_BUFFER_SIZE);pump(&f,1);
    CHECK(f.raw.tx_length==RAW_BUFFER_SIZE&&f.raw.owned);
    CHECK(send(f.client,data,1,0)==1);pump(&f,1);CHECK(f.raw.stats.dropped_datagrams==3);
    f.now+=RAW_STALL_MS;pump(&f,1);CHECK(!f.raw.owned&&f.raw.stats.stalls==1&&f.fake.total==0);
    f.fake.blocked=0;pump(&f,RAW_OWNER_GUARD_MS+1);
    CHECK(send(f.client,"x",1,0)==1);pump(&f,20);CHECK(f.fake.total==1);
    total=f.fake.total;f.now+=RAW_UDP_IDLE_MS;pump(&f,1);CHECK(!f.raw.owned);
    other=client(&f,1);CHECK(send(other,"y",1,0)==1);pump(&f,1);CHECK(f.fake.total==total);
    pump(&f,RAW_OWNER_GUARD_MS+1);CHECK(send(other,"z",1,0)==1);pump(&f,20);CHECK(f.fake.total==total+1);
    close(other);finish(&f);
}
static void tcp_halfclose_and_fault(void)
{
    fixture_t f;unsigned char answer[32];unsigned n;
    init(&f,0);CHECK(send(f.client,"halfclose",9,0)==9);CHECK(shutdown(f.client,SHUT_WR)==0);
    pump(&f,100);n=read_all(f.client,answer,sizeof(answer));CHECK(n==9&&!memcmp(answer,"halfclose",9));
    CHECK(f.raw.eof&&f.raw.owned);pump(&f,RAW_EOF_IDLE_MS+1);CHECK(!f.raw.owned);
    finish(&f);
    init(&f,1);CHECK(send(f.client,"a",1,0)==1);pump(&f,20);
    f.fake.hard=1;pump(&f,1);CHECK(f.port.state==PORT_RECOVERING&&!f.raw.owned);
    CHECK(f.port.stats.backend_failures==1);f.fake.hard=0;pump(&f,10);CHECK(f.port.state==PORT_READY);
    finish(&f);
}
static void unowned_drain(void)
{
    fixture_t f;init(&f,1);memcpy(f.fake.bytes,"old",3);f.fake.length=3;
    pump(&f,1);CHECK(f.raw.stats.discarded_serial==3&&f.raw.guard);
    CHECK(send(f.client,"new",3,0)==3);pump(&f,1);CHECK(f.fake.total==0);
    pump(&f,RAW_OWNER_GUARD_MS+1);CHECK(send(f.client,"new",3,0)==3);pump(&f,30);CHECK(f.fake.total==3);
    finish(&f);
}
static void independent_ports(void)
{
    fixture_t a,b;unsigned char answer[16];unsigned n;
    init(&a,1);init(&b,0);a.fake.blocked=1;
    CHECK(send(a.client,"blocked",7,0)==7);CHECK(send(b.client,"live",4,0)==4);
    pump(&a,20);pump(&b,20);n=read_all(b.client,answer,sizeof(answer));
    CHECK(n==4&&!memcmp(answer,"live",4));CHECK(a.fake.total==0&&b.fake.total==4);
    a.now+=RAW_STALL_MS;pump(&a,1);CHECK(!a.raw.owned&&b.raw.owned);
    CHECK(send(b.client,"still-live",10,0)==10);pump(&b,30);
    n=read_all(b.client,answer,sizeof(answer));CHECK(n==10&&!memcmp(answer,"still-live",10));
    finish(&a);finish(&b);
}
static void failed_flush_fences_owner(void)
{
    fixture_t f;int other;unsigned total;
    init(&f,1);CHECK(send(f.client,"old",3,0)==3);pump(&f,30);
    total=f.fake.total;f.fake.flush_hard=1;
    f.now+=RAW_UDP_IDLE_MS;pump(&f,1);
    CHECK(!f.raw.owned&&f.port.state==PORT_RECOVERING);
    pump(&f,RAW_OWNER_GUARD_MS+20);
    other=client(&f,1);CHECK(send(other,"foreign",7,0)==7);pump(&f,20);
    CHECK(!f.raw.owned&&f.fake.total==total);
    f.fake.flush_hard=0;pump(&f,20);
    CHECK(!f.raw.owned&&f.raw.guard);
    pump(&f,RAW_OWNER_GUARD_MS+1);
    CHECK(send(other,"new",3,0)==3);pump(&f,30);
    CHECK(f.raw.owned&&f.fake.total==total+3);
    CHECK(!memcmp(f.fake.written+total,"new",3));
    close(other);finish(&f);
}
static void tcp_backpressure(void)
{
    fixture_t f;unsigned char data[2048];unsigned i;
    init(&f,0);f.fake.blocked=1;f.fake.echo=0;
    for(i=0;i<sizeof(data);++i)data[i]=(unsigned char)i;
    CHECK(send(f.client,data,sizeof(data),0)==(ssize_t)sizeof(data));pump(&f,20);
    CHECK(f.raw.tx_length==RAW_BUFFER_SIZE&&f.fake.total==0);
    CHECK(f.raw.stats.network_received==RAW_BUFFER_SIZE);
    f.fake.blocked=0;pump(&f,1200);
    CHECK(f.fake.total==sizeof(data)&&!memcmp(f.fake.written,data,sizeof(data)));
    CHECK(f.raw.tx_length==0&&f.raw.stats.network_received==sizeof(data));
    finish(&f);
}
static void udp_packet_boundaries(void)
{
    fixture_t f;unsigned char data[RAW_BUFFER_SIZE+1U],out[RAW_BUFFER_SIZE+1U];unsigned i;
    init(&f,1);f.fake.echo=0;CHECK(send(f.client,"x",1,0)==1);pump(&f,1);
    for(i=0;i<sizeof(data);++i)data[i]=(unsigned char)i;
    memcpy(f.fake.bytes,data,sizeof(data));f.fake.length=sizeof(data);
    pump(&f,147); /* 146 seven-byte reads, then two bytes fill the packet. */
    CHECK(recv(f.client,out,sizeof(out),MSG_DONTWAIT)==RAW_BUFFER_SIZE);
    CHECK(!memcmp(out,data,RAW_BUFFER_SIZE));
    pump(&f,1);CHECK(f.raw.rx_length==1);
    pump(&f,RAW_PACKET_GAP_MS-1);CHECK(recv(f.client,out,sizeof(out),MSG_DONTWAIT)<0);
    pump(&f,1);CHECK(recv(f.client,out,sizeof(out),MSG_DONTWAIT)==1&&out[0]==data[RAW_BUFFER_SIZE]);
    finish(&f);
}
int main(void)
{
    bytes_unchanged(0);bytes_unchanged(1);udp_limits_and_expiry();tcp_halfclose_and_fault();unowned_drain();independent_ports();failed_flush_fences_owner();tcp_backpressure();udp_packet_boundaries();
    printf("RAW checks=%u failures=%u sizeof_listener=%u\n",checks,failures,(unsigned)sizeof(raw_serial_listener_t));
    return failures?1:0;
}
