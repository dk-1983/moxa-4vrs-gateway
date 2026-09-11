#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

#include "gateway/gateway_real_adapter.h"

static volatile sig_atomic_t running=1;
typedef struct raw_trace_context {
    port_runtime_t *runtime;
    unsigned int calls;
    unsigned int positive_reads;
    unsigned int temporary_errors;
    unsigned int recovery_reads;
    unsigned int leading_anomalies;
} raw_trace_context_t;

static void stop_running(int signal_number) { (void)signal_number;running=0; }
static core_tick_t now_ms(void)
{ struct timespec value;if(clock_gettime(CLOCK_MONOTONIC,&value)<0)return 0;return(core_tick_t)((unsigned long)value.tv_sec*1000UL+(unsigned long)value.tv_nsec/1000000UL); }
static unsigned long now_us(void *context)
{ struct timespec value;(void)context;if(clock_gettime(CLOCK_MONOTONIC,&value)<0)return 0;return(unsigned long)value.tv_sec*1000000UL+(unsigned long)value.tv_nsec/1000UL; }
static void pause_one_ms(void)
{ struct timeval delay;delay.tv_sec=0;delay.tv_usec=1000;select(0,0,0,0,&delay); }
static void raw_read_trace(void *context,const uart_read_trace_event_t *event)
{
    raw_trace_context_t *trace=(raw_trace_context_t *)context;unsigned int i;
    ++trace->calls;if(event->phase==UART_READ_RECOVERY)++trace->recovery_reads;
    if(event->result<0&&(event->error==EAGAIN||event->error==EWOULDBLOCK||event->error==EINTR)){++trace->temporary_errors;return;}
    if(event->result>0)++trace->positive_reads;
    fprintf(stderr,"UART_READ mono_us=%lu tx_us=%lu elapsed_us=%lu now_ms=%u core_id=%u generation=%u state=%u phase=%u result=%d errno=%d before=%u after=%u tx_complete=%u tx_at_ms=%u elapsed_ms=%u recovery_since_previous=%u bytes=",
            event->monotonic_us,event->tx_completed_us,event->elapsed_since_tx_us,event->now,event->transaction_id,event->generation,
            trace->runtime?(unsigned int)trace->runtime->state:~0U,
            (unsigned int)event->phase,event->result,event->error,
            event->rx_length_before,event->rx_length_after,event->tx_complete,
            event->tx_completed_at,event->elapsed_since_tx,
            event->recovery_since_previous_transaction);
    for(i=0;i<event->byte_count;++i)fprintf(stderr,"%02X",event->bytes[i]);
    fprintf(stderr,"\n");
    if(event->phase==UART_READ_RESPONSE&&event->result>0&&
       event->rx_length_before==0U&&event->bytes[0]!=0x08U){
        ++trace->leading_anomalies;
        fprintf(stderr,"RAW_LEADING_ANOMALY expected=08 actual=%02X core_id=%u generation=%u mono_us=%lu elapsed_us=%lu\n",
                event->bytes[0],event->transaction_id,event->generation,
                event->monotonic_us,event->elapsed_since_tx_us);
        running=0;
    }
}
static void print_health(const gateway_controller_t *controller,const char *label)
{
    gateway_health_t health;unsigned int i;gateway_controller_health(controller,&health);
    printf("%s ready=%u enabled=%u clients=%u completed=%u",label,health.ready_ports,health.enabled_ports,health.active_clients,health.completed_transactions);
    for(i=0;i<GATEWAY_PORT_COUNT;++i)printf(" P%u=%u",i+1U,(unsigned int)health.port[i].lifecycle);
    printf("\n");fflush(stdout);
}

int main(int argc,char **argv)
{
    gateway_controller_t controller;gateway_port_config_t config[GATEWAY_PORT_COUNT];
    gateway_port_binding_t bindings[GATEWAY_PORT_COUNT];gateway_real_adapter_t p1;
    raw_trace_context_t trace;unsigned int i;int initialized=0;int result=0;
    if(argc!=2||strcmp(argv[1],"--confirmed-orchestrated-p1-trm138-read-only")!=0){fprintf(stderr,"refusing physical P1 gateway without exact guard\n");return 2;}
    memset(bindings,0,sizeof(bindings));gateway_configuration_defaults(config);
    config[0].enabled=1;config[0].mode=SERIAL_MODE_RS485_2W;config[0].baud=115200UL;
    config[0].data_bits=8;config[0].parity=PARITY_NONE;config[0].stop_bits=1;
    config[0].transport=TRANSPORT_MODBUS_TCP;config[0].endpoint_port=1502U;
    for(i=1;i<GATEWAY_PORT_COUNT;++i)config[i].enabled=0;
    memcpy(config[0].bind_address,"10.0.2.15",10U);
    if(gateway_real_adapter_init(&p1,0,&config[0],uart_posix_syscalls(),0,gateway_modbus_listener_driver(),0)!=0)return 3;
    bindings[0]=gateway_real_adapter_binding(&p1);
    if(gateway_controller_init(&controller,config,bindings)!=0)return 4;
    gateway_real_adapter_attach(&p1,&controller.ports[0].runtime);initialized=1;
    memset(&trace,0,sizeof(trace));trace.runtime=&controller.ports[0].runtime;
    uart_backend_set_trace(&p1.uart,1);uart_backend_set_trace_clock(&p1.uart,now_us,0);uart_backend_set_read_trace(&p1.uart,raw_read_trace,&trace);
    gateway_controller_start(&controller,now_ms());
    for(i=0;i<3000U&&controller.ports[0].lifecycle==GATEWAY_PORT_STARTING;++i){gateway_controller_step(&controller,now_ms());pause_one_ms();}
    if(controller.ports[0].lifecycle!=GATEWAY_PORT_READY){fprintf(stderr,"P1_START_FAILED lifecycle=%u error=%u\n",(unsigned int)controller.ports[0].lifecycle,(unsigned int)controller.ports[0].last_error);result=5;running=0;}
    else {modbus_tcp_listener_set_trace(&p1.listener,1);signal(SIGTERM,stop_running);signal(SIGINT,stop_running);print_health(&controller,"ORCHESTRATED_GATEWAY_READY");}
    while(running){gateway_controller_step(&controller,now_ms());if(controller.ports[0].runtime.stats.completed>=500U){fprintf(stderr,"OBSERVATION_TARGET_REACHED completed=%u\n",controller.ports[0].runtime.stats.completed);running=0;}pause_one_ms();}
    if(initialized){gateway_controller_shutdown(&controller,now_ms());for(i=0;i<3000U&&controller.ports[0].lifecycle==GATEWAY_PORT_STOPPING;++i){gateway_controller_step(&controller,now_ms());pause_one_ms();}print_health(&controller,"ORCHESTRATED_GATEWAY_STOP");if(controller.ports[0].lifecycle!=GATEWAY_PORT_DISABLED)result=6;}
    printf("P1_RESULT state=%u runtime=%u tcp_rx=%u tcp_tx=%u completed=%u timeouts=%u recoveries=%u stale=%u rtu_tx=%u rtu_rx=%u saved_mode=%u fd=%d\n",
           (unsigned int)controller.ports[0].lifecycle,(unsigned int)controller.ports[0].runtime.state,
           p1.listener.stats.rx_bytes,p1.listener.stats.tx_bytes,controller.ports[0].runtime.stats.completed,
           controller.ports[0].runtime.stats.timeouts,controller.ports[0].runtime.stats.recoveries,
           controller.ports[0].runtime.stats.stale_responses,p1.uart.stats.bytes_written,p1.uart.stats.bytes_read,
           p1.uart.saved_mode,p1.uart.fd);
    printf("RAW_READ_SUMMARY calls=%u positive=%u temporary=%u recovery_reads=%u leading_anomalies=%u\n",trace.calls,trace.positive_reads,trace.temporary_errors,trace.recovery_reads,trace.leading_anomalies);
    return result;
}
