#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include "network/modbus_tcp_listener.h"
#include "uart/uart_backend.h"
static volatile sig_atomic_t running=1;static void stopit(int s){(void)s;running=0;}
static core_tick_t nowms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)<0)return 0;return(core_tick_t)((unsigned long)t.tv_sec*1000UL+(unsigned long)t.tv_nsec/1000000UL);}
int main(int argc,char**argv){core_port_config_t c;uart_backend_t uart;port_runtime_t port;modbus_tcp_listener_t listener;struct timeval pause_time;unsigned int i;port_state_t prior;
 if(argc!=2||strcmp(argv[1],"--confirmed-trm138-fc04-read-only")!=0){fprintf(stderr,"refusing guard\n");return 2;}
 memset(&c,0,sizeof(c));c.revision=1;c.mode=1;c.baud=115200;c.data_bits=8;c.stop_bits=1;c.endpoint_port=1502;
 if(uart_backend_init(&uart,0,&c,uart_posix_syscalls(),0)<0)return 3;uart_backend_set_trace(&uart,1);uart_backend_set_frame_probe(&uart,modbus_rtu_frame_probe,0);
 port_runtime_init(&port,0,&c,uart_backend_ops(),&uart);if(port_runtime_start(&port,nowms(),1000)<0)return 4;
 for(i=0;i<2000&&port.state!=PORT_READY;++i){port_runtime_step(&port,nowms());pause_time.tv_sec=0;pause_time.tv_usec=1000;select(0,0,0,0,&pause_time);}if(port.state!=PORT_READY)return 5;
 modbus_tcp_listener_init(&listener,&port);modbus_tcp_listener_set_trace(&listener,1);if(modbus_tcp_listener_open(&listener,"10.0.2.15",1502)<0){port_runtime_request_stop(&port,nowms(),1000);return 6;}
 signal(SIGTERM,stopit);signal(SIGINT,stopit);printf("GATEWAY_READY saved_mode=%u saved_termios=%d\n",uart.saved_mode,uart.saved_termios_valid);fflush(stdout);
 prior=port.state;while(running){modbus_tcp_listener_step(&listener,nowms());if(port.state!=prior){fprintf(stderr,"PORT_STATE from=%u to=%u timeouts=%u recoveries=%u stale=%u queue=%u high_water=%u\n",(unsigned int)prior,(unsigned int)port.state,port.stats.timeouts,port.stats.recoveries,port.stats.stale_responses,port.stats.queue_depth,port.stats.queue_high_water);prior=port.state;}pause_time.tv_sec=0;pause_time.tv_usec=1000;select(0,0,0,0,&pause_time);}
 modbus_tcp_listener_close(&listener);port_runtime_request_stop(&port,nowms(),1000);for(i=0;i<2000&&port.state!=PORT_DISABLED;++i){port_runtime_step(&port,nowms());pause_time.tv_sec=0;pause_time.tv_usec=1000;select(0,0,0,0,&pause_time);}
 printf("GATEWAY_STOP state=%d tcp_rx=%u tcp_tx=%u completed=%u timeouts=%u recoveries=%u stale=%u rtu_tx=%u rtu_rx=%u mode_restored=%u\n",port.state,listener.stats.rx_bytes,listener.stats.tx_bytes,port.stats.completed,port.stats.timeouts,port.stats.recoveries,port.stats.stale_responses,uart.stats.bytes_written,uart.stats.bytes_read,uart.saved_mode);return port.state==PORT_DISABLED?0:7;}
