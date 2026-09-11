#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "network/modbus_tcp_listener.h"
#include "network/echo_rtu_backend.h"

static unsigned long checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);}}while(0)

static core_port_config_t cfg(void){core_port_config_t c;memset(&c,0,sizeof(c));c.baud=9600;c.data_bits=8;c.stop_bits=1;c.endpoint_port=1502;return c;}
static int connect_client(void)
{
    struct sockaddr_in sa;int fd=socket(AF_INET,SOCK_STREAM,0);int flags;int yes=1;
    if(fd<0)return -1;
    memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_port=htons(1502);
    inet_aton("127.0.0.1",&sa.sin_addr);if(connect(fd,(struct sockaddr*)&sa,sizeof(sa))<0){close(fd);return -1;}
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    flags=fcntl(fd,F_GETFL,0);fcntl(fd,F_SETFL,flags|O_NONBLOCK);return fd;
}
static unsigned int frame(unsigned char *b,unsigned short tid)
{b[0]=(unsigned char)(tid>>8);b[1]=(unsigned char)tid;b[2]=b[3]=b[4]=0;b[5]=6;b[6]=1;b[7]=3;b[8]=0;b[9]=1;b[10]=0;b[11]=1;return 12;}
static unsigned int frame_fc4(unsigned char *b,unsigned short tid,unsigned char unit)
{b[0]=(unsigned char)(tid>>8);b[1]=(unsigned char)tid;b[2]=b[3]=b[4]=0;b[5]=6;b[6]=unit;b[7]=4;b[8]=b[9]=0;b[10]=0;b[11]=1;return 12;}
static int receive_exact(int fd,modbus_tcp_listener_t*l,core_tick_t *now,unsigned char*out,unsigned int n)
{
    unsigned int got=0,spins=0;ssize_t r;
    while(got<n&&spins++<1000U){modbus_tcp_listener_step(l,(*now)++);r=recv(fd,out+got,n-got,0);
        if(r>0)got+=(unsigned int)r;else if(r==0)return -1;else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)return -1;}
    if(got!=n)fprintf(stderr,"receive timeout got=%u need=%u clients=%u core_state=%d core_q=%u invalid_response=%u tx_count=%u fd=%d tx_bytes=%u\n",
                      got,n,modbus_tcp_listener_client_count(l),(int)l->port->state,l->port->queue.count,
                      l->stats.invalid_responses,l->clients[0].tx_count,l->clients[0].fd,l->stats.tx_bytes);
    return got==n?0:-1;
}
static void basic_and_stream(modbus_tcp_listener_t*l,core_tick_t*now)
{
    unsigned char b[64],out[64];unsigned int n=frame(b,0x1001);int fd=connect_client();unsigned int i;
    CHECK(fd>=0);modbus_tcp_listener_step(l,(*now)++);
    for(i=0;i<n;++i){CHECK(send(fd,b+i,1,0)==1);modbus_tcp_listener_step(l,(*now)++);}
    CHECK(receive_exact(fd,l,now,out,n)==0);CHECK(memcmp(b,out,n)==0);
    frame(b+n,0x1002);frame(b+2*n,0x1003);CHECK(send(fd,b,n*3U,0)==(ssize_t)(n*3U));
    CHECK(receive_exact(fd,l,now,out,n*3U)==0);CHECK(memcmp(b,out,n*3U)==0);
    frame(b,0x1005);CHECK(send(fd,b,n,0)==(ssize_t)n);shutdown(fd,SHUT_WR);
    CHECK(receive_exact(fd,l,now,out,n)==0);CHECK(memcmp(b,out,n)==0);close(fd);
    for(i=0;i<20;++i)modbus_tcp_listener_step(l,(*now)++);
}
static void limits_and_malformed(modbus_tcp_listener_t*l,core_tick_t*now)
{
    int fds[9];unsigned int i;unsigned char bad[7]={0,1,0,1,0,2,1};
    for(i=0;i<9;++i){fds[i]=connect_client();CHECK(fds[i]>=0);modbus_tcp_listener_step(l,(*now)++);}
    CHECK(modbus_tcp_listener_client_count(l)==8U);CHECK(l->stats.refused>=1U);
    CHECK(send(fds[0],bad,sizeof(bad),0)==(ssize_t)sizeof(bad));
    for(i=0;i<5;++i)modbus_tcp_listener_step(l,(*now)++);
    CHECK(l->stats.malformed>=1U);
    for(i=0;i<9;++i)close(fds[i]);
    for(i=0;i<20;++i)modbus_tcp_listener_step(l,(*now)++);
}
static void epoch_disconnect(modbus_tcp_listener_t*l,core_tick_t*now)
{
    unsigned char b[12];unsigned char bad[7]={0,2,0,1,0,2,1};int a=connect_client();int replacement;unsigned int i;frame(b,0x1001);
    modbus_tcp_listener_step(l,(*now)++);CHECK(send(a,b,sizeof(b),0)==(ssize_t)sizeof(b));
    modbus_tcp_listener_step(l,(*now)++);
    CHECK(send(a,bad,sizeof(bad),0)==(ssize_t)sizeof(bad));
    modbus_tcp_listener_step(l,(*now)++);close(a);
    replacement=connect_client();CHECK(replacement>=0);modbus_tcp_listener_step(l,(*now)++);
    modbus_tcp_listener_step(l,(*now)++);
    CHECK(l->stats.closed>=1U);CHECK(l->stats.stale_completions>=1U);close(replacement);
    for(i=0;i<5;++i)modbus_tcp_listener_step(l,(*now)++);
}
static void timeout_policy(modbus_tcp_listener_t*l,echo_rtu_backend_t*b,core_tick_t*now)
{
    unsigned char request[12],response[16];unsigned int before=l->stats.gateway_target_no_response;
    int a=connect_client(),other=connect_client();unsigned int i;
    CHECK(a>=0&&other>=0);modbus_tcp_listener_step(l,(*now)++);modbus_tcp_listener_step(l,(*now)++);
    frame_fc4(request,0x2000,1);CHECK(send(a,request,12,0)==12);CHECK(receive_exact(a,l,now,response,12)==0);CHECK(memcmp(request,response,12)==0);
    echo_rtu_backend_suppress(b,1);frame_fc4(request,0x2001,24);CHECK(send(a,request,12,0)==12);
    for(i=0;i<4U;++i)modbus_tcp_listener_step(l,(*now)++);
    *now+=1001U;modbus_tcp_listener_step(l,(*now)++);
    CHECK(receive_exact(a,l,now,response,9)==0);
    {static const unsigned char expected[9]={0x20,0x01,0,0,0,3,24,0x84,0x0B};CHECK(memcmp(response,expected,9)==0);}
    CHECK(l->stats.gateway_target_no_response==before+1U&&l->stats.timeout_exceptions_queued>=1U);
    for(i=0;i<10U;++i)modbus_tcp_listener_step(l,(*now)++);
    frame_fc4(request,0x2002,1);CHECK(send(a,request,12,0)==12);CHECK(receive_exact(a,l,now,response,12)==0);CHECK(memcmp(request,response,12)==0);
    frame_fc4(request,0x3001,1);CHECK(send(other,request,12,0)==12);CHECK(receive_exact(other,l,now,response,12)==0);CHECK(memcmp(request,response,12)==0);
    CHECK(l->port->state==PORT_READY&&l->port->stats.stale_responses==0U);
    close(a);close(other);for(i=0;i<10U;++i)modbus_tcp_listener_step(l,(*now)++);
}
static void timeout_epoch_obsolete(modbus_tcp_listener_t*l,echo_rtu_backend_t*b,core_tick_t*now)
{
    unsigned char request[12],bad[7]={0,9,0,1,0,2,1},response[12];unsigned int before=l->stats.timeout_exceptions_obsolete_epoch;
    int old=connect_client(),replacement;unsigned int i;
    CHECK(old>=0);modbus_tcp_listener_step(l,(*now)++);echo_rtu_backend_suppress(b,1);
    frame_fc4(request,0x4001,24);CHECK(send(old,request,12,0)==12);for(i=0;i<3U;++i)modbus_tcp_listener_step(l,(*now)++);
    CHECK(send(old,bad,sizeof(bad),0)==(ssize_t)sizeof(bad));modbus_tcp_listener_step(l,(*now)++);close(old);
    replacement=connect_client();CHECK(replacement>=0);modbus_tcp_listener_step(l,(*now)++);
    *now+=1001U;modbus_tcp_listener_step(l,(*now)++);
    CHECK(l->stats.timeout_exceptions_obsolete_epoch==before+1U);
    for(i=0;i<10U;++i)modbus_tcp_listener_step(l,(*now)++);
    frame_fc4(request,0x4002,1);CHECK(send(replacement,request,12,0)==12);CHECK(receive_exact(replacement,l,now,response,12)==0);CHECK(memcmp(request,response,12)==0);
    close(replacement);for(i=0;i<5U;++i)modbus_tcp_listener_step(l,(*now)++);
}
static void partial_stall(modbus_tcp_listener_t*l,core_tick_t*now)
{
    unsigned char b[12];int fd=connect_client();unsigned int before=l->stats.closed;
    CHECK(fd>=0);modbus_tcp_listener_step(l,(*now)++);frame(b,0x5001);
    CHECK(send(fd,b,3,0)==3);modbus_tcp_listener_step(l,(*now)++);
    *now+=MODBUS_INPUT_TIMEOUT;modbus_tcp_listener_step(l,(*now)++);
    CHECK(l->stats.closed==before+1U);close(fd);
}
static void soak(modbus_tcp_listener_t*l,core_tick_t*now)
{
    unsigned char b[12],out[12];unsigned int i,j;int fd[8];unsigned int completed[8]={0,0,0,0,0,0,0,0};
    for(j=0;j<8U;++j){fd[j]=connect_client();CHECK(fd[j]>=0);modbus_tcp_listener_step(l,(*now)++);}
    for(i=0;i<100000U;++i){j=i%8U;frame(b,(unsigned short)(i*17U+j));CHECK(send(fd[j],b,sizeof(b),0)==(ssize_t)sizeof(b));
        CHECK(receive_exact(fd[j],l,now,out,sizeof(out))==0);CHECK(memcmp(b,out,sizeof(b))==0);++completed[j];}
    for(j=0;j<8U;++j){CHECK(completed[j]==12500U);close(fd[j]);}
    for(i=0;i<20;++i)modbus_tcp_listener_step(l,(*now)++);
    printf("fairness=%u,%u,%u,%u,%u,%u,%u,%u\n",completed[0],completed[1],completed[2],completed[3],completed[4],completed[5],completed[6],completed[7]);
}
static void reconnect_storm(modbus_tcp_listener_t*l,core_tick_t*now)
{
    unsigned int i,j;int fd;
    for(i=0;i<100U;++i){fd=connect_client();CHECK(fd>=0);modbus_tcp_listener_step(l,(*now)++);close(fd);
        for(j=0;j<3U;++j)modbus_tcp_listener_step(l,(*now)++);}
    CHECK(modbus_tcp_listener_client_count(l)==0U);
}
static void output_fault_tests(void)
{
    modbus_tcp_listener_t l;port_runtime_t port;echo_rtu_backend_t backend;core_port_config_t c=cfg();
    int pair[2];int other[2];int flags;unsigned char data[MBAP_ADU_MAX];unsigned int i;
    echo_rtu_backend_init(&backend);port_runtime_init(&port,0,&c,echo_rtu_backend_ops(),&backend);port.state=PORT_READY;
    modbus_tcp_listener_init(&l,&port);CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,other)==0);
    flags=fcntl(pair[0],F_GETFL,0);fcntl(pair[0],F_SETFL,flags|O_NONBLOCK);flags=fcntl(other[0],F_GETFL,0);fcntl(other[0],F_SETFL,flags|O_NONBLOCK);
    l.clients[0].fd=pair[0];l.clients[0].id=1;l.clients[0].epoch=1;l.clients[1].fd=other[0];l.clients[1].id=2;l.clients[1].epoch=1;
    modbus_dispatcher_connect(&l.dispatcher,1,1);modbus_dispatcher_connect(&l.dispatcher,2,1);memset(data,0x5a,sizeof(data));l.now=10;
    for(i=0;i<MODBUS_TX_QUEUE_MAX;++i)CHECK(modbus_tcp_listener_queue_response(&l,1,1,data,sizeof(data))==0);
    CHECK(l.clients[0].tx_count==4U);CHECK(modbus_tcp_listener_queue_response(&l,1,1,data,sizeof(data))==-3);
    CHECK(l.stats.tx_overflow==1U&&l.clients[0].fd<0&&l.clients[1].fd>=0);close(pair[1]);
    while(send(other[0],data,sizeof(data),MSG_NOSIGNAL)>0){}
    l.now=20;CHECK(modbus_tcp_listener_queue_response(&l,2,1,data,sizeof(data))==0);
    modbus_tcp_listener_step(&l,20+MODBUS_WRITE_TIMEOUT+1U);
    CHECK(l.stats.write_deadline_expired==1U&&l.clients[1].fd<0&&l.clients[1].tx_count==0U);
    close(other[1]);modbus_tcp_listener_close(&l);
    {
        modbus_tcp_listener_t tl;port_runtime_t tp;echo_rtu_backend_t tb;int blocked[2];
        modbus_tcp_request_t request;int server_flags;
        echo_rtu_backend_init(&tb);port_runtime_init(&tp,0,&c,echo_rtu_backend_ops(),&tb);tp.state=PORT_READY;
        modbus_tcp_listener_init(&tl,&tp);CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,blocked)==0);
        server_flags=fcntl(blocked[0],F_GETFL,0);fcntl(blocked[0],F_SETFL,server_flags|O_NONBLOCK);
        while(send(blocked[0],data,sizeof(data),MSG_NOSIGNAL)>0){}
        tl.clients[0].fd=blocked[0];tl.clients[0].id=1;tl.clients[0].epoch=1;
        CHECK(modbus_dispatcher_connect(&tl.dispatcher,1,1)==0);tl.now=10;
        for(i=0;i<MODBUS_TX_QUEUE_MAX;++i)CHECK(modbus_tcp_listener_queue_response(&tl,1,1,data,sizeof(data))==0);
        memset(&request,0,sizeof(request));request.client_id=1;request.client_epoch=1;
        request.adu.transaction_id=0x5001;request.adu.unit_id=24;request.adu.pdu_length=5;
        request.adu.pdu[0]=4;request.adu.pdu[4]=1;echo_rtu_backend_suppress(&tb,1);
        CHECK(modbus_tcp_submit(&tp,&request,10,1000,1000,0)==CORE_ACCEPTED);
        modbus_tcp_listener_step(&tl,10);modbus_tcp_listener_step(&tl,11);
        modbus_tcp_listener_step(&tl,1012);modbus_tcp_listener_step(&tl,1013);
        CHECK(tl.stats.gateway_target_no_response==1U);
        CHECK(tl.stats.timeout_exception_output_failures==1U&&tl.stats.tx_overflow==1U);
        CHECK(tl.clients[0].fd<0&&tl.clients[0].tx_count==0U&&tp.state==PORT_RECOVERING);
        close(blocked[1]);modbus_tcp_listener_close(&tl);
    }
}
static unsigned int fd_count(void){DIR*d=opendir("/proc/self/fd");struct dirent*e;unsigned int n=0;if(!d)return 0;while((e=readdir(d))!=0)if(e->d_name[0]!='.')++n;closedir(d);return n;}
static void lifecycle_test(void)
{
    unsigned int i,before=fd_count();
    for(i=0;i<500U;++i){modbus_tcp_listener_t l;port_runtime_t p;echo_rtu_backend_t b;core_port_config_t c=cfg();int fd;
        echo_rtu_backend_init(&b);port_runtime_init(&p,0,&c,echo_rtu_backend_ops(),&b);p.state=PORT_READY;
        modbus_tcp_listener_init(&l,&p);CHECK(modbus_tcp_listener_open(&l,"127.0.0.1",1502)==0);
        fd=connect_client();CHECK(fd>=0);modbus_tcp_listener_step(&l,(core_tick_t)i);close(fd);
        modbus_tcp_listener_step(&l,(core_tick_t)i+1U);modbus_tcp_listener_close(&l);
        CHECK(modbus_tcp_listener_client_count(&l)==0U&&p.queue.count==0U&&!p.has_active);}
    CHECK(fd_count()==before);
}
int main(void)
{
    modbus_tcp_listener_t listener;port_runtime_t port;echo_rtu_backend_t backend;core_port_config_t c=cfg();core_tick_t now=1;
    echo_rtu_backend_init(&backend);port_runtime_init(&port,0,&c,echo_rtu_backend_ops(),&backend);
    CHECK(port_runtime_start(&port,now,10)==0);port_runtime_step(&port,now);CHECK(port.state==PORT_READY);
    modbus_tcp_listener_init(&listener,&port);CHECK(modbus_tcp_listener_open(&listener,"127.0.0.1",1502)==0);
    basic_and_stream(&listener,&now);limits_and_malformed(&listener,&now);epoch_disconnect(&listener,&now);
    timeout_policy(&listener,&backend,&now);timeout_epoch_obsolete(&listener,&backend,&now);
    partial_stall(&listener,&now);
    reconnect_storm(&listener,&now);soak(&listener,&now);
    modbus_tcp_listener_close(&listener);CHECK(modbus_tcp_listener_client_count(&listener)==0U);
    output_fault_tests();
    lifecycle_test();
    printf("listener checks=%lu failed=%lu requests=%u clients_max=%u refused=%u stale=%u memory=%u\n",
           checks,failures,backend.starts,listener.stats.max_clients,listener.stats.refused,
           listener.stats.stale_completions,modbus_tcp_listener_memory_bytes());return failures?1:0;
}
