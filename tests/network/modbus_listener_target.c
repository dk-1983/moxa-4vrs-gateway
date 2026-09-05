#define _POSIX_C_SOURCE 199309L

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "network/modbus_tcp_listener.h"
#include "network/echo_rtu_backend.h"

static volatile sig_atomic_t running=1;
static void stop_handler(int signal_number){(void)signal_number;running=0;}
static core_tick_t ticks_ms(void)
{
    struct timespec value;
    if(clock_gettime(CLOCK_MONOTONIC,&value)<0)return 0;
    return (core_tick_t)((unsigned long)value.tv_sec*1000UL+
                         (unsigned long)value.tv_nsec/1000000UL);
}
int main(int argc,char **argv)
{
    core_port_config_t config;port_runtime_t port;echo_rtu_backend_t backend;
    modbus_tcp_listener_t listener;core_tick_t now;
    if(argc!=3 || strcmp(argv[1],"--confirmed-mock-listener-1502")!=0) {
        fprintf(stderr,"refusing: exact guard and bind address required\n");return 2;
    }
    memset(&config,0,sizeof(config));config.baud=9600;config.data_bits=8;
    config.stop_bits=1;config.endpoint_port=MODBUS_LISTENER_TEST_PORT;
    echo_rtu_backend_init(&backend);port_runtime_init(&port,0,&config,echo_rtu_backend_ops(),&backend);
    now=ticks_ms();if(port_runtime_start(&port,now,1000)<0)return 3;
    port_runtime_step(&port,now);if(port.state!=PORT_READY)return 4;
    modbus_tcp_listener_init(&listener,&port);
    if(modbus_tcp_listener_open(&listener,argv[2],MODBUS_LISTENER_TEST_PORT)<0) {
        perror("listener open");return 5;
    }
    signal(SIGINT,stop_handler);signal(SIGTERM,stop_handler);
    printf("LISTENER_READY address=%s port=%u pid=%ld memory=%u\n",argv[2],
           MODBUS_LISTENER_TEST_PORT,(long)getpid(),modbus_tcp_listener_memory_bytes());fflush(stdout);
    while(running){
        struct timeval pause_time;
        modbus_tcp_listener_step(&listener,ticks_ms());
        pause_time.tv_sec=0;pause_time.tv_usec=1000;
        select(0,0,0,0,&pause_time);
    }
    modbus_tcp_listener_close(&listener);
    printf("LISTENER_STOP accepted=%u refused=%u closed=%u malformed=%u rx=%u tx=%u stale=%u steps=%u backend_starts=%u clients=%u\n",
           listener.stats.accepted,listener.stats.refused,listener.stats.closed,
           listener.stats.malformed,listener.stats.rx_bytes,listener.stats.tx_bytes,
           listener.stats.stale_completions,listener.stats.scheduler_steps,
           backend.starts,modbus_tcp_listener_client_count(&listener));
    return 0;
}
