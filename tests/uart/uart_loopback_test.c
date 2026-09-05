#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/times.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/mock_backend.h"
#include "uart/uart_backend.h"

#define LOOPBACK_TIMEOUT_TICKS 300U
#define LOOPBACK_MAX_STEPS 2000U
#define LOOPBACK_SOAK_TRANSACTIONS 2000U

typedef struct result_capture {
    unsigned int calls;
    core_transaction_status_t status;
    unsigned int length;
    unsigned char data[CORE_TRANSACTION_PAYLOAD_MAX];
} result_capture_t;

static core_tick_t ticks_now(void)
{
    struct tms ignored;
    return (core_tick_t)times(&ignored);
}

static int count_open_fds(void)
{
    long limit=sysconf(_SC_OPEN_MAX); int fd,count=0;
    if(limit<0 || limit>4096) limit=4096;
    for(fd=0;fd<(int)limit;++fd) if(fcntl(fd,F_GETFD)!=-1) ++count;
    return count;
}

static void scheduler_pause(void)
{
    struct timeval delay;
    delay.tv_sec=0; delay.tv_usec=1000;
    select(0,0,0,0,&delay);
}

static core_port_config_t config(unsigned int revision, unsigned int baud,
                                 unsigned int parity)
{
    core_port_config_t c;
    memset(&c,0,sizeof(c)); c.revision=revision; c.mode=0; c.baud=baud;
    c.data_bits=8; c.parity=parity; c.stop_bits=1; c.endpoint_port=502;
    return c;
}

static void completed(void *context, const core_transaction_t *transaction,
                      core_transaction_status_t status,
                      const unsigned char *response, unsigned int length)
{
    result_capture_t *capture=context; (void)transaction;
    capture->calls++; capture->status=status; capture->length=length;
    if(response && length<=sizeof(capture->data)) memcpy(capture->data,response,length);
}

static int wait_state(port_runtime_t ports[8], port_state_t wanted,
                      unsigned int max_steps)
{
    unsigned int step,p; core_tick_t now;
    for(step=0;step<max_steps;++step) {
        now=ticks_now(); for(p=0;p<8;++p) port_runtime_step(&ports[p],now);
        if(ports[0].state==wanted) return 0;
        scheduler_pause();
    }
    return -1;
}

static int transact(port_runtime_t ports[8], result_capture_t *capture,
                    const unsigned char *payload, unsigned int length)
{
    unsigned int metadata[CORE_TRANSPORT_METADATA_WORDS]={0,0,0,0};
    core_operation_t operation={CORE_OPERATION_TX_THEN_RX,CORE_RESPONSE_FIXED_LENGTH,length};
    unsigned int before=capture->calls, step, p; core_tick_t now=ticks_now();
    for(p=1;p<8;++p) {
        unsigned char marker=(unsigned char)p;
        if(ports[p].stats.accepted==0 &&
           (mock_backend_add_plan((mock_backend_t *)ports[p].backend_context,
                                  MOCK_IMMEDIATE_SUCCESS,0)!=0 ||
            port_runtime_submit(&ports[p],&marker,1,2,p,now,100,100,0)!=CORE_ACCEPTED))
             return -1;
    }
    if(port_runtime_submit_operation(&ports[0],payload,length,1,1,metadata,&operation,
          now,LOOPBACK_TIMEOUT_TICKS,LOOPBACK_TIMEOUT_TICKS,0)!=CORE_ACCEPTED)
        return -1;
    for(step=0;step<LOOPBACK_MAX_STEPS && capture->calls==before;++step) {
        now=ticks_now(); for(p=0;p<8;++p) port_runtime_step(&ports[p],now);
        scheduler_pause();
    }
    if(capture->calls==before || capture->status!=CORE_TX_COMPLETED ||
       capture->length!=length || memcmp(capture->data,payload,length)!=0)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    static const unsigned int lengths[]={1,9,65,256};
    static const unsigned int bauds[]={9600,19200,115200};
    static const unsigned int parity[]={0,1,0};
    port_runtime_t ports[8]; mock_backend_t mocks[7]; uart_backend_t uart;
    result_capture_t capture; core_port_config_t c=config(1,9600,0);
    unsigned char payload[256]; unsigned int i,j,p; int fd_before,fd_after;

    if(argc!=2 || strcmp(argv[1],"--confirmed-rs232-loopback")!=0) {
        fprintf(stderr,"refusing: install P1 RS232 TXD-RXD loopback and pass --confirmed-rs232-loopback\n");
        return 2;
    }
    memset(&capture,0,sizeof(capture));
    if(uart_backend_init(&uart,0,&c,uart_posix_syscalls(),0)!=0) return 3;
    port_runtime_init(&ports[0],0,&c,uart_backend_ops(),&uart);
    port_runtime_set_completion(&ports[0],completed,&capture);
    for(p=1;p<8;++p) { mock_backend_init(&mocks[p-1]); port_runtime_init(&ports[p],p,&c,mock_backend_ops(),&mocks[p-1]); }
    fd_before=count_open_fds();
    for(p=0;p<8;++p) if(port_runtime_start(&ports[p],ticks_now(),100)!=0) return 4;
    if(wait_state(ports,PORT_READY,500)!=0) return 5;

    for(i=0;i<sizeof(bauds)/sizeof(bauds[0]);++i) {
        c=config(i+2,bauds[i],parity[i]);
        if(port_runtime_request_reconfigure(&ports[0],&c,ticks_now(),100)!=0 ||
           wait_state(ports,PORT_READY,500)!=0) return 6;
        for(j=0;j<sizeof(lengths)/sizeof(lengths[0]);++j) {
            for(p=0;p<lengths[j];++p) payload[p]=(unsigned char)(p^lengths[j]^i);
            if(transact(ports,&capture,payload,lengths[j])!=0) return 7;
        }
    }
    c=config(10,115200,0);
    for(i=0;i<LOOPBACK_SOAK_TRANSACTIONS;++i) {
        unsigned int length=1+(i%256);
        for(p=0;p<length;++p) payload[p]=(unsigned char)(p^i);
        if(transact(ports,&capture,payload,length)!=0) return 8;
    }
    for(p=0;p<8;++p) port_runtime_request_stop(&ports[p],ticks_now(),100);
    if(wait_state(ports,PORT_DISABLED,500)!=0) return 9;
    fd_after=count_open_fds();
    for(p=1;p<8;++p) if(ports[p].stats.completed!=1) return 10;
    printf("loopback_tx=%u loopback_rx=%u completed=%u timeouts=%u recoveries=%u fd_before=%d fd_after=%d peers_completed=%u status=OK\n",
      uart.stats.bytes_written,uart.stats.bytes_read,capture.calls,
      ports[0].stats.timeouts,ports[0].stats.recoveries,fd_before,fd_after,
      ports[1].stats.completed);
    return 0;
}
