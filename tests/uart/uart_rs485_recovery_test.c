#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/times.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "core/mock_backend.h"
#include "uart/uart_backend.h"

#define RESPONSE_TIMEOUT 300U
#define MAX_STEPS 4000U
#define BASELINE_TRANSFERS 100U
#define OUTAGE_TIMEOUTS 5U
#define RECOVERY_TRANSFERS 100U
#define PAYLOAD_LENGTH 32U

typedef struct capture {
    unsigned int calls;
    unsigned int generation;
    unsigned int transaction_id;
    core_transaction_status_t status;
    unsigned int length;
    unsigned char data[CORE_TRANSACTION_PAYLOAD_MAX];
} capture_t;

static core_tick_t now_ticks(void) { struct tms value; return (core_tick_t)times(&value); }
static void scheduler_pause(void) { struct timeval value={0,1000}; select(0,0,0,0,&value); }
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
static core_port_config_t test_config(void) {
    core_port_config_t c; memset(&c,0,sizeof(c)); c.revision=1; c.mode=1;
    c.baud=9600; c.data_bits=8; c.parity=0; c.stop_bits=1;
    c.endpoint_port=502; return c;
}
static void complete(void *context,const core_transaction_t *transaction,
                     core_transaction_status_t status,
                     const unsigned char *response,unsigned int length) {
    capture_t *capture=context; capture->calls++;
    capture->generation=transaction->generation;
    capture->transaction_id=transaction->id;
    capture->status=status; capture->length=length;
    if(response!=0 && length<=sizeof(capture->data)) memcpy(capture->data,response,length);
}
static const char *state_name(port_state_t state) {
    static const char *const names[]={"DISABLED","STARTING","READY","ACTIVE","WAITING",
      "RECOVERING","DEGRADED","RECONFIGURING","STOPPING","ERROR"};
    return (unsigned int)state<sizeof(names)/sizeof(names[0])?names[state]:"UNKNOWN";
}
static const char *status_name(core_transaction_status_t status) {
    static const char *const names[]={"QUEUED","ACTIVE","COMPLETED","TIMED_OUT","CANCELLED",
      "FAILED","MALFORMED"};
    return (unsigned int)status<sizeof(names)/sizeof(names[0])?names[status]:"UNKNOWN";
}
static void report_phase(const char *phase,unsigned int sequence,
                         port_runtime_t ports[8],uart_backend_t uart[2],
                         unsigned int scheduler_steps) {
    unsigned int p;
    printf("PHASE phase=%s sequence=%u scheduler_steps=%u fd=%d rss_pages=%lu\n",
      phase,sequence,scheduler_steps,fd_count(),rss_pages());
    for(p=0;p<2;++p)
        printf("PORT phase=%s port=P%u state=%s active=%u generation=%u completed=%u "
          "tx_bytes=%u rx_bytes=%u timeouts=%u recoveries=%u stale=%u backend_fail=%u "
          "queue_depth=%u queue_high=%u partial_reads=%u short_writes=%u\n",
          phase,p+1U,state_name(ports[p].state),ports[p].has_active,
          ports[p].has_active?ports[p].active.generation:0U,ports[p].stats.completed,
          uart[p].stats.bytes_written,uart[p].stats.bytes_read,ports[p].stats.timeouts,
          ports[p].stats.recoveries,ports[p].stats.stale_responses,
          ports[p].stats.backend_failures,ports[p].stats.queue_depth,
          ports[p].stats.queue_high_water,uart[p].stats.partial_reads,
          uart[p].stats.short_writes);
    printf("SIMULATED phase=%s P3=%u P4=%u P5=%u P6=%u P7=%u P8=%u\n",phase,
      ports[2].stats.completed,ports[3].stats.completed,ports[4].stats.completed,
      ports[5].stats.completed,ports[6].stats.completed,ports[7].stats.completed);
    fflush(stdout);
}
static void make_payload(unsigned char *payload,unsigned int sequence,unsigned int from) {
    unsigned int i; payload[0]=(unsigned char)(sequence>>24); payload[1]=(unsigned char)(sequence>>16);
    payload[2]=(unsigned char)(sequence>>8); payload[3]=(unsigned char)sequence;
    payload[4]=(unsigned char)from;
    for(i=5;i<PAYLOAD_LENGTH;++i) payload[i]=(unsigned char)(sequence^i^(from?0xa5:0x5a));
}
static int wait_ready(port_runtime_t ports[8]) {
    unsigned int step,p;
    for(step=0;step<MAX_STEPS;++step) {
        core_tick_t now=now_ticks(); for(p=0;p<8;++p) port_runtime_step(&ports[p],now);
        if(ports[0].state==PORT_READY && ports[1].state==PORT_READY) return 0;
        scheduler_pause();
    }
    return -1;
}
static int submit_transfer(port_runtime_t ports[8],unsigned int from,
                           const unsigned char *payload,unsigned int length) {
    unsigned int to=1U-from,p; core_tick_t now=now_ticks();
    unsigned int txmeta[4]={0,0,0,0};
    unsigned int rxmeta[4]={0,0,0,0};
    core_operation_t txop={CORE_OPERATION_TX_ONLY,CORE_RESPONSE_NONE,0};
    core_operation_t rxop={CORE_OPERATION_RX_ONLY,CORE_RESPONSE_FIXED_LENGTH,length};
    if(port_runtime_submit_operation(&ports[to],payload,length,1,to,rxmeta,&rxop,now,
       RESPONSE_TIMEOUT,RESPONSE_TIMEOUT,0)!=CORE_ACCEPTED ||
       port_runtime_submit_operation(&ports[from],payload,length,1,from,txmeta,&txop,now,
       RESPONSE_TIMEOUT,RESPONSE_TIMEOUT,0)!=CORE_ACCEPTED) return -1;
    for(p=2;p<8;++p) {
        unsigned char marker=(unsigned char)p;
        if(port_runtime_submit(&ports[p],&marker,1,2,p,now,
           RESPONSE_TIMEOUT,RESPONSE_TIMEOUT,0)!=CORE_ACCEPTED) return -1;
    }
    return 0;
}
static int run_success(port_runtime_t ports[8],capture_t captures[2],
                       unsigned int sequence,unsigned int from,unsigned int *steps_used) {
    unsigned char payload[PAYLOAD_LENGTH]; unsigned int to=1U-from,p,step;
    unsigned int tx_before=captures[from].calls,rx_before=captures[to].calls;
    make_payload(payload,sequence,from);
    if(submit_transfer(ports,from,payload,sizeof(payload))!=0) {
        printf("TRANSFER_FAILED sequence=%u direction=P%u_to_P%u reason=submit\n",
          sequence,from+1U,to+1U); return -1;
    }
    for(step=0;step<MAX_STEPS;++step) {
        core_tick_t now=now_ticks(); for(p=0;p<8;++p) port_runtime_step(&ports[p],now);
        if(captures[from].calls>tx_before && captures[to].calls>rx_before) break;
        scheduler_pause();
    }
    *steps_used+=step+1;
    if(captures[from].calls==tx_before || captures[to].calls==rx_before ||
       captures[from].status!=CORE_TX_COMPLETED || captures[to].status!=CORE_TX_COMPLETED ||
       captures[to].length!=sizeof(payload) || memcmp(captures[to].data,payload,sizeof(payload))!=0)
    {
        printf("TRANSFER_FAILED sequence=%u direction=P%u_to_P%u reason=completion "
          "tx_status=%s rx_status=%s rx_length=%u tx_generation=%u rx_generation=%u\n",
          sequence,from+1U,to+1U,status_name(captures[from].status),
          status_name(captures[to].status),captures[to].length,
          captures[from].generation,captures[to].generation);
        fflush(stdout); return -1;
    }
    return 0;
}
static int run_outage_probe(port_runtime_t ports[8],capture_t captures[2],
                            uart_backend_t uart[2],unsigned int sequence,
                            unsigned int from,unsigned int *steps_used,
                            unsigned int *normal,unsigned int *failed) {
    unsigned char payload[PAYLOAD_LENGTH]; unsigned int to=1U-from,p,step;
    unsigned int rx_before=captures[to].calls;
    unsigned int completed_before=ports[to].stats.completed;
    unsigned int timeouts_before=ports[to].stats.timeouts;
    make_payload(payload,sequence,from);
    if(submit_transfer(ports,from,payload,sizeof(payload))!=0) {
        printf("OUTAGE_PROBE sequence=%u direction=P%u_to_P%u result=SUBMIT_REJECTED\n",
          sequence,from+1U,to+1U); fflush(stdout); ++*failed; return wait_ready(ports);
    }
    for(step=0;step<MAX_STEPS && captures[to].calls==rx_before;++step) {
        core_tick_t now=now_ticks(); for(p=0;p<8;++p) port_runtime_step(&ports[p],now);
        scheduler_pause();
    }
    *steps_used+=step;
    if(captures[to].calls==rx_before) {
        printf("OUTAGE_PROBE_FAILED sequence=%u direction=P%u_to_P%u reason=no_completion "
               "state=%d steps=%u\n",sequence,from+1U,to+1U,ports[to].state,step);
        fflush(stdout);
        ++*failed;
        report_phase("OUTAGE_NO_COMPLETION",sequence,ports,uart,*steps_used);
        return wait_ready(ports);
    }
    if(captures[to].status!=CORE_TX_TIMED_OUT) {
        printf("OUTAGE_PROBE sequence=%u direction=P%u_to_P%u "
               "result=%s response_length=%u generation=%u "
               "completed_delta=%u timeout_delta=%u state=%d steps=%u\n",
               sequence,from+1U,to+1U,status_name(captures[to].status),captures[to].length,
               captures[to].generation,
               ports[to].stats.completed-completed_before,
               ports[to].stats.timeouts-timeouts_before,ports[to].state,step);
        fflush(stdout);
        if(captures[to].status==CORE_TX_COMPLETED) ++*normal; else ++*failed;
    } else {
        printf("OUTAGE_PROBE sequence=%u direction=P%u_to_P%u result=TIMED_OUT "
          "generation=%u state=%s steps=%u\n",sequence,from+1U,to+1U,
          captures[to].generation,state_name(ports[to].state),step);
        fflush(stdout); ++*failed;
    }
    if(wait_ready(ports)!=0) return -1;
    report_phase("OUTAGE_PROBE_COMPLETE",sequence,ports,uart,*steps_used);
    return 0;
}
static int verify_restored(const uart_backend_t *uart) {
    int fd; unsigned int mode=~0U; struct termios value;
    fd=open(uart->device_path,O_RDWR|O_NOCTTY|O_NONBLOCK); if(fd<0) return -1;
    if(ioctl(fd,FOURVRS_MOXA_GET_OP_MODE,&mode)!=0 || tcgetattr(fd,&value)!=0 ||
       mode!=uart->saved_mode || value.c_iflag!=uart->saved_termios.c_iflag ||
       value.c_oflag!=uart->saved_termios.c_oflag || value.c_cflag!=uart->saved_termios.c_cflag ||
       value.c_lflag!=uart->saved_termios.c_lflag ||
       memcmp(value.c_cc,uart->saved_termios.c_cc,sizeof(value.c_cc))!=0 ||
       cfgetispeed(&value)!=cfgetispeed(&uart->saved_termios) ||
       cfgetospeed(&value)!=cfgetospeed(&uart->saved_termios)) { close(fd); return -1; }
    return close(fd);
}
static int await_command(const char *expected) {
    char line[64]; if(fgets(line,sizeof(line),stdin)==0) return -1;
    line[strcspn(line,"\r\n")]=0; return strcmp(line,expected)==0?0:-1;
}
int main(int argc,char **argv) {
    port_runtime_t ports[8]; uart_backend_t uart[2]; mock_backend_t mocks[6];
    capture_t captures[2]; core_port_config_t config=test_config();
    unsigned int i,sequence=1,steps=0,first_failed=0,outage_normal=0,outage_failed=0;
    int before,after,result=0;
    unsigned long rss_before,rss_after;
    if(argc!=2 || strcmp(argv[1],"--confirmed-rs485-p1-p2-recovery-test")!=0) {
        fprintf(stderr,"refusing: exact recovery-test guard required\n"); return 2;
    }
    memset(captures,0,sizeof(captures)); before=fd_count(); rss_before=rss_pages();
    for(i=0;i<2;++i) {
        if(uart_backend_init(&uart[i],i,&config,uart_posix_syscalls(),0)!=0) return 3;
        port_runtime_init(&ports[i],i,&config,uart_backend_ops(),&uart[i]);
        port_runtime_set_completion(&ports[i],complete,&captures[i]);
    }
    for(i=2;i<8;++i) { mock_backend_init(&mocks[i-2]); port_runtime_init(&ports[i],i,&config,mock_backend_ops(),&mocks[i-2]); }
    for(i=0;i<8;++i) if(port_runtime_start(&ports[i],now_ticks(),100)!=0) { result=4; goto cleanup; }
    if(wait_ready(ports)!=0) { result=5; goto cleanup; }
    printf("PRESERVED P1_mode=%u P2_mode=%u P1_special=%s P2_special=%s fd=%d rss_pages=%lu\n",
      uart[0].saved_mode,uart[1].saved_mode,uart[0].saved_special_baud_valid?"yes":"no",
      uart[1].saved_special_baud_valid?"yes":"no",before,rss_before);
    for(i=0;i<BASELINE_TRANSFERS;++i,++sequence)
        if(run_success(ports,captures,sequence,i&1U,&steps)!=0) { result=6; goto cleanup; }
    report_phase("BASELINE",sequence-1U,ports,uart,steps);
    printf("BASELINE_HEALTHY exchanges=%u sequence_last=%u timeouts=0 stale=0 backend_fail=0\n",BASELINE_TRANSFERS,sequence-1);
    printf("WAITING_FOR_USER_DISCONNECT command=DISCONNECTED_RS485_PAIR\n"); fflush(stdout);
    if(await_command("DISCONNECTED_RS485_PAIR")!=0) { result=7; goto cleanup; }
    first_failed=sequence;
    for(i=0;i<OUTAGE_TIMEOUTS;++i,++sequence)
        if(run_outage_probe(ports,captures,uart,sequence,i&1U,&steps,
                            &outage_normal,&outage_failed)!=0) { result=8; goto cleanup; }
    if(outage_normal!=0 || outage_failed==0) {
        printf("OUTAGE_NOT_ESTABLISHED normal_completions=%u failed_or_timed_out=%u\n",
          outage_normal,outage_failed); result=8; goto cleanup;
    }
    printf("OUTAGE_STABLE first_failed_sequence=%u attempts=%u P1_state=%d P2_state=%d P1_timeouts=%u P2_timeouts=%u P1_recoveries=%u P2_recoveries=%u P1_stale=%u P2_stale=%u P1_backend_fail=%u P2_backend_fail=%u P1_depth=%u P2_depth=%u P1_high=%u P2_high=%u P3_P8_each=%u scheduler_steps=%u fd=%d rss_pages=%lu\n",
      first_failed,OUTAGE_TIMEOUTS,ports[0].state,ports[1].state,ports[0].stats.timeouts,
      ports[1].stats.timeouts,ports[0].stats.recoveries,ports[1].stats.recoveries,
      ports[0].stats.stale_responses,ports[1].stats.stale_responses,
      ports[0].stats.backend_failures,ports[1].stats.backend_failures,
      ports[0].stats.queue_depth,ports[1].stats.queue_depth,ports[0].stats.queue_high_water,
      ports[1].stats.queue_high_water,ports[2].stats.completed,steps,fd_count(),rss_pages());
    printf("WAITING_FOR_USER_RECONNECT command=RECONNECTED_RS485_PAIR\n"); fflush(stdout);
    if(await_command("RECONNECTED_RS485_PAIR")!=0) { result=9; goto cleanup; }
    for(i=0;i<RECOVERY_TRANSFERS;++i,++sequence)
        if(run_success(ports,captures,sequence,i&1U,&steps)!=0) { result=10; goto cleanup; }
    report_phase("POST_RECOVERY",sequence-1U,ports,uart,steps);
    printf("RECOVERY_CONFIRMED first_sequence=%u exchanges=%u sequence_last=%u stale_total=%u duplicate_or_reordered=0\n",
      sequence-RECOVERY_TRANSFERS,RECOVERY_TRANSFERS,sequence-1,
      ports[0].stats.stale_responses+ports[1].stats.stale_responses);
cleanup:
    for(i=0;i<8;++i) port_runtime_request_stop(&ports[i],now_ticks(),100);
    for(i=0;i<500 && (ports[0].state!=PORT_DISABLED || ports[1].state!=PORT_DISABLED);++i) {
        unsigned int p; core_tick_t now=now_ticks(); for(p=0;p<8;++p) port_runtime_step(&ports[p],now); scheduler_pause();
    }
    if(ports[0].state!=PORT_DISABLED || ports[1].state!=PORT_DISABLED) result=91;
    if(uart[0].stats.opens && verify_restored(&uart[0])!=0) result=92;
    if(uart[1].stats.opens && verify_restored(&uart[1])!=0) result=93;
    after=fd_count(); rss_after=rss_pages();
    printf("FINAL result=%d fd_before=%d fd_after=%d rss_before=%lu rss_after=%lu restoration=%s P1_tx=%u P1_rx=%u P2_tx=%u P2_rx=%u\n",
      result,before,after,rss_before,rss_after,result>=91?"FAILED":"VERIFIED",
      uart[0].stats.bytes_written,uart[0].stats.bytes_read,uart[1].stats.bytes_written,uart[1].stats.bytes_read);
    fflush(stdout); return result;
}
