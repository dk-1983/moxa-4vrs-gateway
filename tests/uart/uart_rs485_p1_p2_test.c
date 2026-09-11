#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/times.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "core/mock_backend.h"
#include "uart/uart_backend.h"

#define TEST_TIMEOUT 300U
#define MAX_STEPS 4000U
#define SOAK_TRANSFERS 2000U

typedef struct capture {
    unsigned int calls;
    core_transaction_status_t status;
    unsigned int length;
    unsigned char data[CORE_TRANSACTION_PAYLOAD_MAX];
} capture_t;

static core_tick_t now_ticks(void) { struct tms t; return (core_tick_t)times(&t); }
static void pause_step(void) { struct timeval t={0,1000}; select(0,0,0,0,&t); }
static int fd_count(void) {
    long limit=sysconf(_SC_OPEN_MAX); int fd,count=0;
    if(limit<0 || limit>4096) limit=4096;
    for(fd=0;fd<(int)limit;++fd) if(fcntl(fd,F_GETFD)!=-1) ++count;
    return count;
}
static unsigned long rss_pages(void) {
    FILE *file=fopen("/proc/self/statm","r"); unsigned long size=0,rss=0;
    if(file==0) return 0;
    if(fscanf(file,"%lu %lu",&size,&rss)!=2) rss=0;
    fclose(file); return rss;
}
static int verify_restored(const uart_backend_t *uart) {
    int fd,mode_result; unsigned int mode=~0U; struct termios value;
    fd=open(uart->device_path,O_RDWR|O_NOCTTY|O_NONBLOCK); if(fd<0) return -1;
    mode_result=ioctl(fd,FOURVRS_MOXA_GET_OP_MODE,&mode);
    if(tcgetattr(fd,&value)!=0 || mode_result!=0 || !uart->saved_termios_valid ||
       !uart->saved_mode_valid || mode!=uart->saved_mode ||
       value.c_iflag!=uart->saved_termios.c_iflag ||
       value.c_oflag!=uart->saved_termios.c_oflag ||
       value.c_cflag!=uart->saved_termios.c_cflag ||
       value.c_lflag!=uart->saved_termios.c_lflag ||
       memcmp(value.c_cc,uart->saved_termios.c_cc,sizeof(value.c_cc))!=0 ||
       cfgetispeed(&value)!=cfgetispeed(&uart->saved_termios) ||
       cfgetospeed(&value)!=cfgetospeed(&uart->saved_termios)) { close(fd); return -1; }
    return close(fd);
}
static core_port_config_t make_config(unsigned int revision,unsigned int baud,unsigned int parity) {
    core_port_config_t c; memset(&c,0,sizeof(c)); c.revision=revision;
    c.mode=1; c.baud=baud; c.data_bits=8; c.parity=parity; c.stop_bits=1;
    c.endpoint_port=502; return c;
}
static void done(void *ctx,const core_transaction_t *tx,core_transaction_status_t status,
                 const unsigned char *data,unsigned int length) {
    capture_t *c=ctx; (void)tx; c->calls++; c->status=status; c->length=length;
    if(data && length<=sizeof(c->data)) memcpy(c->data,data,length);
}
static int wait_ports(port_runtime_t ports[8],port_state_t state) {
    unsigned int step,p; for(step=0;step<MAX_STEPS;++step) {
        core_tick_t now=now_ticks(); for(p=0;p<8;++p) port_runtime_step(&ports[p],now);
        if(ports[0].state==state && ports[1].state==state) return 0;
        pause_step();
    } return -1;
}
static int transfer(port_runtime_t ports[8],capture_t captures[2],unsigned int from,
                    const unsigned char *payload,unsigned int length) {
    unsigned int to=1-from,p,before_tx=captures[from].calls,before_rx=captures[to].calls;
    unsigned int txmeta[4]={0,0,0,0};
    unsigned int rxmeta[4]={0,0,0,0};
    core_operation_t txop={CORE_OPERATION_TX_ONLY,CORE_RESPONSE_NONE,0};
    core_operation_t rxop={CORE_OPERATION_RX_ONLY,CORE_RESPONSE_FIXED_LENGTH,length};
    core_tick_t now=now_ticks();
    if(port_runtime_submit_operation(&ports[to],payload,length,1,to,rxmeta,&rxop,now,TEST_TIMEOUT,TEST_TIMEOUT,0)!=CORE_ACCEPTED ||
       port_runtime_submit_operation(&ports[from],payload,length,1,from,txmeta,&txop,now,TEST_TIMEOUT,TEST_TIMEOUT,0)!=CORE_ACCEPTED) return -1;
    for(p=2;p<8;++p) {
        unsigned char marker=(unsigned char)p;
        if(port_runtime_submit(&ports[p],&marker,1,2,p,now,TEST_TIMEOUT,TEST_TIMEOUT,0)!=CORE_ACCEPTED) return -1;
    }
    for(p=0;p<MAX_STEPS;++p) {
        unsigned int i; now=now_ticks(); for(i=0;i<8;++i) port_runtime_step(&ports[i],now);
        if(captures[from].calls>before_tx && captures[to].calls>before_rx) break;
        pause_step();
    }
    if(captures[from].calls==before_tx || captures[to].calls==before_rx ||
       captures[from].status!=CORE_TX_COMPLETED || captures[to].status!=CORE_TX_COMPLETED ||
       captures[to].length!=length || memcmp(captures[to].data,payload,length)!=0) return -1;
    return 0;
}
int main(int argc,char **argv) {
    static const unsigned int lengths[]={1,9,65,256};
    static const unsigned int bauds[]={9600,19200,115200};
    static const unsigned int parity[]={0,1,0};
    port_runtime_t ports[8]; uart_backend_t uart[2]; mock_backend_t mocks[6];
    capture_t captures[2]; core_port_config_t c=make_config(1,9600,0);
    unsigned char payload[256]; unsigned int cfg,n,i,p; int before,after,result=0;
    unsigned long rss_before,rss_after;
    if(argc!=2 || strcmp(argv[1],"--confirmed-rs485-2w-p1-p2")!=0) {
        fprintf(stderr,"refusing: exact --confirmed-rs485-2w-p1-p2 guard required\n"); return 2;
    }
    memset(captures,0,sizeof(captures)); before=fd_count(); rss_before=rss_pages();
    for(i=0;i<2;++i) {
        if(uart_backend_init(&uart[i],i,&c,uart_posix_syscalls(),0)!=0) return 3;
        port_runtime_init(&ports[i],i,&c,uart_backend_ops(),&uart[i]);
        port_runtime_set_completion(&ports[i],done,&captures[i]);
    }
    for(i=2;i<8;++i) { mock_backend_init(&mocks[i-2]); port_runtime_init(&ports[i],i,&c,mock_backend_ops(),&mocks[i-2]); }
    for(i=0;i<8;++i) if(port_runtime_start(&ports[i],now_ticks(),100)!=0) { result=4; goto fail; }
    if(wait_ports(ports,PORT_READY)!=0) { result=5; goto fail; }
    printf("preserved P1_mode=%u P2_mode=%u P1_special=%s P2_special=%s\n",
      uart[0].saved_mode,uart[1].saved_mode,
      uart[0].saved_special_baud_valid?"yes":"no",
      uart[1].saved_special_baud_valid?"yes":"no");
    for(cfg=0;cfg<3;++cfg) {
        c=make_config(cfg+2,bauds[cfg],parity[cfg]);
        for(i=0;i<2;++i) if(port_runtime_request_reconfigure(&ports[i],&c,now_ticks(),100)!=0) { result=6; goto fail; }
        if(wait_ports(ports,PORT_READY)!=0) { result=7; goto fail; }
        for(n=0;n<4;++n) for(i=0;i<2;++i) {
            for(p=0;p<lengths[n];++p) payload[p]=(unsigned char)(p^lengths[n]^cfg^(i?0xa5:0x5a));
            if(transfer(ports,captures,i,payload,lengths[n])!=0) { fprintf(stderr,"transfer_failed cfg=%u direction=P%u_to_P%u length=%u\n",cfg,i+1,2-i,lengths[n]); result=11; goto fail; }
        }
    }
    for(n=0;n<SOAK_TRANSFERS;++n) {
        unsigned int from=n&1U,length=1U+(n%256U);
        for(p=0;p<length;++p) payload[p]=(unsigned char)(p^n^(from?0xc3:0x3c));
        if(transfer(ports,captures,from,payload,length)!=0) { fprintf(stderr,"soak_failed n=%u\n",n); result=12; goto fail; }
    }
    for(i=0;i<8;++i) port_runtime_request_stop(&ports[i],now_ticks(),100);
    if(wait_ports(ports,PORT_DISABLED)!=0) return 8;
    if(verify_restored(&uart[0])!=0 || verify_restored(&uart[1])!=0) return 9;
    after=fd_count(); rss_after=rss_pages();
    for(i=2;i<8;++i) if(ports[i].stats.completed!=24+SOAK_TRANSFERS) return 10;
    printf("P1 completed=%u tx=%u rx=%u timeouts=%u recoveries=%u stale=%u backend_fail=%u partial_read=%u short_write=%u high_water=%u\n",
      ports[0].stats.completed,uart[0].stats.bytes_written,uart[0].stats.bytes_read,ports[0].stats.timeouts,ports[0].stats.recoveries,ports[0].stats.stale_responses,ports[0].stats.backend_failures,uart[0].stats.partial_reads,uart[0].stats.short_writes,ports[0].stats.queue_high_water);
    printf("P2 completed=%u tx=%u rx=%u timeouts=%u recoveries=%u stale=%u backend_fail=%u partial_read=%u short_write=%u high_water=%u\n",
      ports[1].stats.completed,uart[1].stats.bytes_written,uart[1].stats.bytes_read,ports[1].stats.timeouts,ports[1].stats.recoveries,ports[1].stats.stale_responses,ports[1].stats.backend_failures,uart[1].stats.partial_reads,uart[1].stats.short_writes,ports[1].stats.queue_high_water);
    printf("P3_P8_completed_each=%u fd_before=%d fd_after=%d rss_pages_before=%lu rss_pages_after=%lu restoration=VERIFIED status=OK\n",ports[2].stats.completed,before,after,rss_before,rss_after); return before==after?0:13;
fail:
    for(i=0;i<8;++i) port_runtime_request_stop(&ports[i],now_ticks(),100);
    if(wait_ports(ports,PORT_DISABLED)!=0) return 90;
    if((uart[0].stats.opens && verify_restored(&uart[0])!=0) ||
       (uart[1].stats.opens && verify_restored(&uart[1])!=0)) return 91;
    return result;
}
