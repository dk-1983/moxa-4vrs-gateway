#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "network/modbus_udp_listener.h"
#include "network/echo_rtu_backend.h"
#include "modbus/modbus_crc.h"

static unsigned int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while(0)
typedef struct fixture {
    modbus_udp_listener_t listener;
    port_runtime_t runtime;
    echo_rtu_backend_t backend;
    core_tick_t now;
    int client;
    struct sockaddr_in peer, server;
} fixture_t;

static void tick(fixture_t *f, unsigned int count)
{
    unsigned int i;
    for(i=0;i<count;++i) { modbus_udp_listener_step_io(&f->listener,f->now); port_runtime_step(&f->runtime,f->now++); }
}
static void setup(fixture_t *f, int rtu)
{
    core_port_config_t c;
    socklen_t len;
    memset(f,0,sizeof(*f)); memset(&c,0,sizeof(c)); c.baud=9600;c.data_bits=8;c.stop_bits=1;c.revision=1;
    echo_rtu_backend_init(&f->backend);
    port_runtime_init(&f->runtime,0,&c,echo_rtu_backend_ops(),&f->backend);
    CHECK(port_runtime_start(&f->runtime,0,100)==0);
    port_runtime_step(&f->runtime,0); f->now=1;
    modbus_udp_listener_init(&f->listener,&f->runtime,rtu);
    CHECK(modbus_udp_listener_open(&f->listener,"127.0.0.1",0)==0);
    len=sizeof(f->server);CHECK(getsockname(f->listener.fd,(struct sockaddr*)&f->server,&len)==0);
    f->client=socket(AF_INET,SOCK_DGRAM,0); CHECK(f->client>=0);
    memset(&f->peer,0,sizeof(f->peer));f->peer.sin_family=AF_INET;f->peer.sin_addr.s_addr=inet_addr("127.0.0.1");
    CHECK(bind(f->client,(struct sockaddr*)&f->peer,sizeof(f->peer))==0);
    len=sizeof(f->peer);CHECK(getsockname(f->client,(struct sockaddr*)&f->peer,&len)==0);
    CHECK(fcntl(f->client,F_SETFL,O_NONBLOCK)==0);
}
static void teardown(fixture_t *f)
{
    modbus_udp_listener_close(&f->listener);close(f->client);
    CHECK(port_runtime_request_stop(&f->runtime,f->now,100)==0);
    tick(f,10);CHECK(f->runtime.state==PORT_DISABLED);
}
static unsigned int request(unsigned char *out, int rtu, unsigned short tid)
{
    unsigned char frame[]={1,6,0,12,0,37,0,0};
    unsigned short crc=modbus_crc16(frame,6);frame[6]=(unsigned char)crc;frame[7]=(unsigned char)(crc>>8);
    if(rtu){memcpy(out,frame,8);return 8;}
    out[0]=(unsigned char)(tid>>8);out[1]=(unsigned char)tid;out[2]=out[3]=out[4]=0;out[5]=6;
    memcpy(out+6,frame,6);return 12;
}
static int receive(fixture_t*f,unsigned char*out)
{
    unsigned int i;
    struct sockaddr_in src; socklen_t len=sizeof(src);ssize_t n;
    for(i=0;i<30;++i){tick(f,1);n=recvfrom(f->client,out,MBAP_ADU_MAX,0,(struct sockaddr*)&src,&len);if(n>=0){CHECK(src.sin_port==f->server.sin_port);return(int)n;}}
    return -1;
}
static void send_request(fixture_t*f,const unsigned char*b,unsigned int n)
{CHECK(sendto(f->client,b,n,0,(struct sockaddr*)&f->server,sizeof(f->server))==(ssize_t)n);}

static void round_trip(int rtu)
{
    fixture_t f;unsigned char b[MBAP_ADU_MAX],out[MBAP_ADU_MAX];unsigned int n,starts;
    setup(&f,rtu);n=request(b,rtu,0x1234);send_request(&f,b,n);
    CHECK(receive(&f,out)==(int)n);CHECK(!memcmp(b,out,n));starts=f.backend.starts;
    send_request(&f,b,n);CHECK(receive(&f,out)==(int)n);CHECK(!memcmp(b,out,n));
    CHECK(f.backend.starts==starts+(rtu?1U:0U));
    if(!rtu){b[11]^=1;send_request(&f,b,n);tick(&f,10);CHECK(recv(f.client,out,sizeof(out),0)<0);CHECK(f.listener.stats.conflicts==1);CHECK(f.backend.starts==starts);
        b[1]++;send_request(&f,b,n);CHECK(receive(&f,out)==(int)n);CHECK(f.backend.starts==starts+1U);}
    teardown(&f);
}
static void malformed(int rtu)
{
    fixture_t f;unsigned char b[600];unsigned int n,i,accepted;
    setup(&f,rtu);n=request(b,rtu,1);accepted=f.listener.stats.accepted;
    for(i=0;i<n;++i)CHECK(modbus_udp_listener_receive(&f.listener,&f.peer,b,i,f.now)<0);
    memset(b,0,sizeof(b));CHECK(modbus_udp_listener_receive(&f.listener,&f.peer,b,sizeof(b),f.now)<0);
    n=request(b,rtu,1);if(rtu)b[n-1]^=1;else b[2]=1;
    CHECK(modbus_udp_listener_receive(&f.listener,&f.peer,b,n,f.now)<0);
    n=request(b,rtu,1);memcpy(b+n,b,n);send_request(&f,b,n*2);tick(&f,10);
    CHECK(f.listener.stats.accepted==accepted);CHECK(f.backend.starts==0);
    n=request(b,rtu,2);send_request(&f,b,n);{unsigned char out[MBAP_ADU_MAX];CHECK(receive(&f,out)==(int)n);}
    teardown(&f);
}
static void limits_and_dedup(int rtu)
{
    fixture_t f;unsigned char b[MBAP_ADU_MAX];unsigned int i,n;struct sockaddr_in peer;
    setup(&f,rtu);n=request(b,rtu,1);peer=f.peer;
    for(i=0;i<MODBUS_UDP_PEERS;++i){peer.sin_port=htons((unsigned short)(20000+i));CHECK(modbus_udp_listener_receive(&f.listener,&peer,b,n,f.now)==0);}
    CHECK(modbus_udp_listener_peer_count(&f.listener)==MODBUS_UDP_PEERS);
    peer.sin_port=htons(21000);CHECK(modbus_udp_listener_receive(&f.listener,&peer,b,n,f.now)==-2);
    peer.sin_port=htons(20000);
    for(i=0;i<1000;++i)CHECK(modbus_udp_listener_receive(&f.listener,&peer,b,n,f.now)==1);
    CHECK(f.listener.stats.duplicates==1000);CHECK(f.backend.starts==0);
    tick(&f,100);CHECK(f.backend.starts==MODBUS_UDP_PEERS);
    f.now+=MODBUS_UDP_IDLE_TIMEOUT;tick(&f,1);CHECK(modbus_udp_listener_peer_count(&f.listener)==0);
    teardown(&f);
}
static void timeout_and_close(int rtu)
{
    fixture_t f;unsigned char b[MBAP_ADU_MAX],out[MBAP_ADU_MAX];unsigned int n;
    setup(&f,rtu);n=request(b,rtu,7);echo_rtu_backend_suppress(&f.backend,1);send_request(&f,b,n);tick(&f,1100);
    if(rtu) CHECK(recv(f.client,out,sizeof(out),0)<0);
    else {CHECK(recv(f.client,out,sizeof(out),0)==9);CHECK(out[0]==0&&out[1]==7&&out[6]==1&&out[7]==0x86&&out[8]==0x0b);}
    CHECK(f.listener.stats.timeouts==1);CHECK(f.backend.starts==1);
    if(!rtu){send_request(&f,b,n);CHECK(receive(&f,out)==9);CHECK(f.backend.starts==1);}
    teardown(&f);
}
static void queue_expiry_and_stale(int rtu)
{
    fixture_t f;unsigned char b[MBAP_ADU_MAX],out[MBAP_ADU_MAX];unsigned int n;
    core_transaction_t old;
    setup(&f,rtu);n=request(b,rtu,11);
    CHECK(modbus_udp_listener_receive(&f.listener,&f.peer,b,n,f.now)==0);
    modbus_udp_listener_step_io(&f.listener,f.now++); /* Queued, not yet transmitted. */
    CHECK(f.runtime.queue.count==1);
    f.now+=MODBUS_UDP_QUEUE_TIMEOUT;tick(&f,2);
    CHECK(!f.listener.peers[0].pending);CHECK(f.backend.starts==0);
    if(rtu)CHECK(recv(f.client,out,sizeof(out),0)<0);
    else{CHECK(recv(f.client,out,sizeof(out),0)==9);CHECK(out[8]==0x0a);}
    n=request(b,rtu,12);send_request(&f,b,n);tick(&f,1);old=f.runtime.active;
    tick(&f,10);CHECK(recv(f.client,out,sizeof(out),0)==(int)n);
    /* A completed transaction cannot emit a second response through a reused peer. */
    f.runtime.completion(f.runtime.completion_context,&old,CORE_TX_COMPLETED,old.payload,old.payload_length);
    CHECK(recv(f.client,out,sizeof(out),0)<0);CHECK(f.listener.stats.stale==1);
    teardown(&f);
}

static void socket_bounds_and_cache_expiry(void)
{
    fixture_t f;unsigned char b[600],out[MBAP_ADU_MAX];unsigned int n,i;
    modbus_udp_listener_t other;
    setup(&f,0);
    modbus_udp_listener_init(&other,&f.runtime,1);
    CHECK(modbus_udp_listener_open(&other,"127.0.0.1",ntohs(f.server.sin_port))<0);
    /* Restore the actual owner's completion callback after the isolated bind test. */
    modbus_udp_listener_close(&other);
    modbus_udp_listener_close(&f.listener);
    modbus_udp_listener_init(&f.listener,&f.runtime,0);
    CHECK(modbus_udp_listener_open(&f.listener,"127.0.0.1",ntohs(f.server.sin_port))==0);
    memset(b,0,sizeof(b));send_request(&f,b,sizeof(b));tick(&f,1);
    CHECK(f.listener.stats.malformed==1);CHECK(f.backend.starts==0);
    n=request(b,0,55);send_request(&f,b,n);CHECK(receive(&f,out)==(int)n);
    f.now+=MODBUS_UDP_CACHE_TIMEOUT;send_request(&f,b,n);CHECK(receive(&f,out)==(int)n);
    CHECK(f.backend.starts==2);
    /* Even a valid packet flood consumes a bounded number per step. */
    for(i=0;i<20U;++i)send_request(&f,b,n);
    i=f.listener.stats.received;modbus_udp_listener_step_io(&f.listener,f.now++);
    CHECK(f.listener.stats.received-i<=MODBUS_UDP_STEP_MAX);
    teardown(&f);
}

static void unsupported_and_broadcast(void)
{
    fixture_t f;unsigned char b[16];unsigned int n;unsigned short crc;
    setup(&f,1);n=request(b,1,0);b[0]=0;crc=modbus_crc16(b,n-2);b[n-2]=(unsigned char)crc;b[n-1]=(unsigned char)(crc>>8);
    CHECK(modbus_udp_listener_receive(&f.listener,&f.peer,b,n,f.now)<0);
    n=request(b,1,0);b[1]=0x17;crc=modbus_crc16(b,n-2);b[n-2]=(unsigned char)crc;b[n-1]=(unsigned char)(crc>>8);
    CHECK(modbus_udp_listener_receive(&f.listener,&f.peer,b,n,f.now)<0);
    CHECK(f.backend.starts==0);teardown(&f);
}

int main(void)
{
    int rtu;
    for(rtu=0;rtu<2;++rtu){round_trip(rtu);malformed(rtu);limits_and_dedup(rtu);timeout_and_close(rtu);queue_expiry_and_stale(rtu);}
    socket_bounds_and_cache_expiry();unsupported_and_broadcast();
    printf("udp checks=%u failures=%u listener_bytes=%u\n",checks,failures,(unsigned int)sizeof(modbus_udp_listener_t));
    return failures?1:0;
}
