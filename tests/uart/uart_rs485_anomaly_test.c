#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <linux/serial.h>

#include "core/port_runtime.h"
#include "uart/uart_backend.h"
#include "uart_anomaly_control.h"

#define GUARD "--confirmed-rs485-2w-p1-p2-anomaly-test"
#define TRANSFERS_PER_DIRECTION 100000U
#define TRANSFER_TIMEOUT 300U
#define MAX_STEPS 4000U
#define PROFILE_COUNT 3U
#define INTENTIONAL_STOP_EXIT 20

typedef struct completion_capture {
    unsigned int calls;
    core_transaction_status_t status;
    unsigned int length;
    unsigned char data[CORE_TRANSACTION_PAYLOAD_MAX];
} completion_capture_t;

typedef struct raw_capture {
    unsigned int direction;
    unsigned int sequence;
    const unsigned char *expected;
    unsigned int expected_length;
    unsigned int positive_reads;
    unsigned int partial_reads;
    unsigned int bytes;
    unsigned int mismatch;
    uart_read_trace_event_t first_mismatch;
    unsigned int mismatch_offset;
    unsigned char expected_byte;
    unsigned char actual_byte;
} raw_capture_t;

typedef struct icount_capture {
    int supported;
    int error;
    struct serial_icounter_struct value;
} icount_capture_t;

static core_tick_t now_ticks(void) { struct tms value; return (core_tick_t)times(&value); }
static unsigned long now_us(void *context) {
    struct timeval value; (void)context;
    if (gettimeofday(&value,0)!=0) return 0;
    return (unsigned long)value.tv_sec*1000000UL+(unsigned long)value.tv_usec;
}
static void pause_us(unsigned int delay) {
    struct timeval value;
    value.tv_sec=(long)(delay/1000000U); value.tv_usec=(long)(delay%1000000U);
    select(0,0,0,0,&value);
}
static int pause_interruptible(unsigned int delay,anomaly_run_control_t *control) {
    while(delay>0U) {
        unsigned int slice=delay>1000U?1000U:delay;
        if(anomaly_control_stop_requested(control)) return 1;
        pause_us(slice); delay-=slice;
    }
    return anomaly_control_stop_requested(control);
}
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
static core_port_config_t config(void) {
    core_port_config_t value; memset(&value,0,sizeof(value));
    value.revision=1; value.mode=1; value.baud=115200; value.data_bits=8;
    value.parity=0; value.stop_bits=1; value.endpoint_port=502;
    return value;
}
static void completed(void *context,const core_transaction_t *transaction,
                      core_transaction_status_t status,const unsigned char *data,
                      unsigned int length) {
    completion_capture_t *capture=(completion_capture_t *)context; (void)transaction;
    capture->calls++; capture->status=status; capture->length=length;
    if(data!=0 && length<=sizeof(capture->data)) memcpy(capture->data,data,length);
}
static void raw_read(void *context,const uart_read_trace_event_t *event) {
    raw_capture_t *capture=(raw_capture_t *)context; unsigned int i,offset;
    if(event->phase!=UART_READ_RESPONSE || event->result<=0) return;
    capture->positive_reads++; capture->bytes+=event->byte_count;
    if(event->byte_count<UART_IO_BUDGET) capture->partial_reads++;
    offset=event->rx_length_before;
    for(i=0;i<event->byte_count;++i) {
        unsigned char expected=offset+i<capture->expected_length?
          capture->expected[offset+i]:0;
        if(!capture->mismatch &&
           (offset+i>=capture->expected_length || event->bytes[i]!=expected)) {
            capture->mismatch=1; capture->first_mismatch=*event;
            capture->mismatch_offset=offset+i; capture->expected_byte=expected;
            capture->actual_byte=event->bytes[i];
        }
    }
}
static int wait_state(port_runtime_t ports[2],port_state_t state) {
    unsigned int step,p;
    for(step=0;step<MAX_STEPS;++step) {
        core_tick_t now=now_ticks();
        for(p=0;p<2;++p) port_runtime_step(&ports[p],now);
        if(ports[0].state==state && ports[1].state==state) return 0;
        pause_us(1000);
    }
    return -1;
}
static void make_payload(unsigned char *payload,unsigned int length,
                         unsigned int sequence,unsigned int direction) {
    unsigned int i; unsigned int state=0x9e3779b9U^sequence^(direction?0xa5a5U:0x5a5aU);
    for(i=0;i<length;++i) {
        state=state*1664525U+1013904223U;
        payload[i]=(unsigned char)(state>>24);
    }
    if((sequence%4U)==0U && length>=3U) {
        payload[0]=0x08; payload[1]=0x04; payload[2]=0x50;
    } else if((sequence%4U)==1U && length>=5U) {
        payload[0]=0x00; payload[1]=0xfe; payload[2]=0xff;
        payload[3]=0x08; payload[4]=0x04;
    } else if((sequence%4U)==2U && length>=2U) {
        payload[0]=0xfe; payload[1]=0xff;
    }
}
static void probe_icount(int fd,icount_capture_t *capture) {
    memset(capture,0,sizeof(*capture)); errno=0;
    if(ioctl(fd,TIOCGICOUNT,&capture->value)==0) capture->supported=1;
    else capture->error=errno;
}
static void print_icount(const char *label,const icount_capture_t *capture) {
    if(!capture->supported) {
        printf("%s supported=NO errno=%d\n",label,capture->error); return;
    }
    printf("%s supported=YES frame=%d parity=%d overrun=%d brk=%d buf_overrun=%d rx=%d tx=%d\n",
      label,capture->value.frame,capture->value.parity,capture->value.overrun,
      capture->value.brk,capture->value.buf_overrun,capture->value.rx,capture->value.tx);
}
static void print_mismatch(const raw_capture_t *capture) {
    const uart_read_trace_event_t *event=&capture->first_mismatch; unsigned int i;
    printf("MISMATCH direction=P%u_to_P%u sequence=%u offset=%u expected=%02X actual=%02X ",
      capture->direction+1U,2U-capture->direction,capture->sequence,
      capture->mismatch_offset,capture->expected_byte,capture->actual_byte);
    printf("mono_us=%lu tx_us=%lu elapsed_us=%lu read_length=%d before=%u after=%u core_id=%u generation=%u bytes=",
      event->monotonic_us,event->tx_completed_us,event->elapsed_since_tx_us,event->result,
      event->rx_length_before,event->rx_length_after,event->transaction_id,event->generation);
    for(i=0;i<event->byte_count;++i) printf("%02X",event->bytes[i]);
    printf("\n");
}
static int transfer(port_runtime_t ports[2],completion_capture_t done[2],
                    raw_capture_t raw[2],unsigned int from,const unsigned char *payload,
                    unsigned int length,unsigned int sequence,
                    anomaly_run_control_t *control) {
    unsigned int to=1U-from,step,before_tx=done[from].calls,before_rx=done[to].calls;
    unsigned int metadata[4]={0,0,0,0}; core_tick_t now=now_ticks();
    core_operation_t txop={CORE_OPERATION_TX_ONLY,CORE_RESPONSE_NONE,0};
    core_operation_t rxop={CORE_OPERATION_RX_ONLY,CORE_RESPONSE_FIXED_LENGTH,length};
    raw[to].direction=from; raw[to].sequence=sequence; raw[to].expected=payload;
    raw[to].expected_length=length; raw[to].mismatch=0;
    control->phase=from==0U?ANOMALY_PHASE_P1_TO_P2:ANOMALY_PHASE_P2_TO_P1;
    if(anomaly_control_stop_requested(control)) return 1;
    if(port_runtime_submit_operation(&ports[to],payload,length,1,to,metadata,&rxop,
       now,TRANSFER_TIMEOUT,TRANSFER_TIMEOUT,0)!=CORE_ACCEPTED) return -1;
    if(port_runtime_submit_operation(&ports[from],payload,length,1,from,metadata,&txop,
       now,TRANSFER_TIMEOUT,TRANSFER_TIMEOUT,0)!=CORE_ACCEPTED) return -1;
    for(step=0;step<MAX_STEPS;++step) {
        now=now_ticks(); port_runtime_step(&ports[0],now); port_runtime_step(&ports[1],now);
        if(raw[to].positive_reads>0U && done[to].calls==before_rx)
            control->phase=ANOMALY_PHASE_PARTIAL_READ;
        if(anomaly_control_stop_requested(control)) return 1;
        if(done[from].calls>before_tx && done[to].calls>before_rx) break;
        pause_us(500);
    }
    if(raw[to].mismatch) { print_mismatch(&raw[to]); return -2; }
    if(done[from].calls==before_tx || done[to].calls==before_rx ||
       done[from].status!=CORE_TX_COMPLETED || done[to].status!=CORE_TX_COMPLETED ||
       done[to].length!=length || memcmp(done[to].data,payload,length)!=0) return -1;
    return 0;
}
static int verify_restored(const uart_backend_t *uart) {
    int fd,result=-1; unsigned int mode=~0U; struct termios value;
    fd=open(uart->device_path,O_RDWR|O_NOCTTY|O_NONBLOCK); if(fd<0) return -1;
    if(ioctl(fd,FOURVRS_MOXA_GET_OP_MODE,&mode)==0 && tcgetattr(fd,&value)==0 &&
       uart->saved_termios_valid && uart->saved_mode_valid && mode==uart->saved_mode &&
       value.c_iflag==uart->saved_termios.c_iflag && value.c_oflag==uart->saved_termios.c_oflag &&
       value.c_cflag==uart->saved_termios.c_cflag && value.c_lflag==uart->saved_termios.c_lflag &&
       memcmp(value.c_cc,uart->saved_termios.c_cc,sizeof(value.c_cc))==0 &&
       cfgetispeed(&value)==cfgetispeed(&uart->saved_termios) &&
       cfgetospeed(&value)==cfgetospeed(&uart->saved_termios)) result=0;
    close(fd); return result;
}
int main(int argc,char **argv) {
    static const unsigned int lengths[]={1,8,9,63,64,65,84,85,86,128,256};
    static const unsigned int spacing_us[PROFILE_COUNT]={20000,5000,1000};
    port_runtime_t ports[2]; uart_backend_t uart[2]; completion_capture_t done[2];
    raw_capture_t raw[2]; icount_capture_t icount_before[2],icount_after[2];
    anomaly_run_control_t control;
    core_port_config_t cfg=config(); unsigned char payload[CORE_TRANSACTION_PAYLOAD_MAX];
    unsigned int direction,sequence,index,profile,p; unsigned int completed_count[2]={0,0};
    int before_fd,after_fd,result=0,restored0=1,restored1=1;
    unsigned long before_rss,after_rss; unsigned long total_bytes[2]={0,0};
    if(argc!=2 || strcmp(argv[1],GUARD)!=0) {
        fprintf(stderr,"refusing: exact %s guard required\n",GUARD); return 2;
    }
    if(anomaly_control_install()!=0) return 3;
    anomaly_control_reset(&control);
    memset(ports,0,sizeof(ports)); memset(uart,0,sizeof(uart));
    uart[0].fd=-1; uart[1].fd=-1;
    memset(done,0,sizeof(done)); memset(raw,0,sizeof(raw));
    memset(icount_before,0,sizeof(icount_before)); memset(icount_after,0,sizeof(icount_after));
    before_fd=fd_count(); before_rss=rss_pages();
    for(p=0;p<2;++p) {
        if(uart_backend_init(&uart[p],p,&cfg,uart_posix_syscalls(),0)!=0) { result=3; goto cleanup; }
        uart_backend_set_trace_clock(&uart[p],now_us,0);
        uart_backend_set_read_trace(&uart[p],raw_read,&raw[p]);
        port_runtime_init(&ports[p],p,&cfg,uart_backend_ops(),&uart[p]);
        port_runtime_set_completion(&ports[p],completed,&done[p]);
        if(port_runtime_start(&ports[p],now_ticks(),100)!=0) { result=4; goto cleanup; }
    }
    if(wait_state(ports,PORT_READY)!=0) { result=5; goto cleanup; }
    printf("PREFLIGHT_CAPTURED P1_mode=%u P2_mode=%u P1_special=%s P2_special=%s\n",
      uart[0].saved_mode,uart[1].saved_mode,uart[0].saved_special_baud_valid?"yes":"no",
      uart[1].saved_special_baud_valid?"yes":"no");
    probe_icount(uart[0].fd,&icount_before[0]); probe_icount(uart[1].fd,&icount_before[1]);
    for(direction=0;direction<2;++direction) {
        for(sequence=0;sequence<TRANSFERS_PER_DIRECTION;++sequence) {
            index=sequence%(sizeof(lengths)/sizeof(lengths[0])); profile=sequence%PROFILE_COUNT;
            make_payload(payload,lengths[index],sequence,direction);
            int transfer_result=transfer(ports,done,raw,direction,payload,lengths[index],sequence,&control);
            if(transfer_result!=0) {
                result=transfer_result==1?INTENTIONAL_STOP_EXIT:10+(int)direction;
                goto cleanup;
            }
            completed_count[direction]++; total_bytes[direction]+=lengths[index];
            control.phase=ANOMALY_PHASE_SPACING;
            if(pause_interruptible(spacing_us[profile],&control)) {
                result=INTENTIONAL_STOP_EXIT; goto cleanup;
            }
        }
    }
    probe_icount(uart[0].fd,&icount_after[0]); probe_icount(uart[1].fd,&icount_after[1]);
cleanup:
    if(!anomaly_control_begin_cleanup(&control)) return 94;
    if(uart[0].fd>=0 && !icount_after[0].supported && icount_after[0].error==0)
        probe_icount(uart[0].fd,&icount_after[0]);
    if(uart[1].fd>=0 && !icount_after[1].supported && icount_after[1].error==0)
        probe_icount(uart[1].fd,&icount_after[1]);
    for(p=0;p<2;++p) if(ports[p].state!=PORT_DISABLED)
        port_runtime_request_stop(&ports[p],now_ticks(),100);
    if((ports[0].state!=PORT_DISABLED || ports[1].state!=PORT_DISABLED) &&
       wait_state(ports,PORT_DISABLED)!=0) result=result?result:90;
    if(uart[0].stats.opens) restored0=verify_restored(&uart[0])==0;
    if(uart[1].stats.opens) restored1=verify_restored(&uart[1])==0;
    if(!restored0) result=91;
    if(!restored1) result=92;
    after_fd=fd_count(); after_rss=rss_pages();
    print_icount("P1_ICOUNT_BEFORE",&icount_before[0]); print_icount("P1_ICOUNT_AFTER",&icount_after[0]);
    print_icount("P2_ICOUNT_BEFORE",&icount_before[1]); print_icount("P2_ICOUNT_AFTER",&icount_after[1]);
    printf("RESULT classification=%s transfers_P1_P2=%u transfers_P2_P1=%u bytes_P1_P2=%lu bytes_P2_P1=%lu ",
      result==INTENTIONAL_STOP_EXIT?"INTENTIONAL_STOP":(result==0?"COMPLETE":"FAILURE"),
      completed_count[0],completed_count[1],total_bytes[0],total_bytes[1]);
    printf("P1_reads=%u P1_partial=%u P2_reads=%u P2_partial=%u fd_before=%d fd_after=%d rss_before=%lu rss_after=%lu restoration=%s status=%d\n",
      raw[0].positive_reads,raw[0].partial_reads,raw[1].positive_reads,raw[1].partial_reads,
      before_fd,after_fd,before_rss,after_rss,restored0&&restored1&&before_fd==after_fd?"VERIFIED":"FAILED",result);
    if(result==0 && before_fd!=after_fd) result=93;
    return result;
}
