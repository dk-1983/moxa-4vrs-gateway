#include <string.h>

#include "gateway/gateway_real_adapter.h"
#include "modbus/modbus_tcp_adapter.h"

static const gateway_listener_driver_t *udp_driver(int rtu);
static const gateway_listener_driver_t *raw_driver(int udp);

static core_port_config_t core_config(const gateway_port_config_t *config)
{
    core_port_config_t c;memset(&c,0,sizeof(c));c.revision=config->revision;
    c.mode=(unsigned int)config->mode;c.baud=(unsigned int)(config->special_baud_enabled?config->special_baud:config->baud);
    c.data_bits=config->data_bits;c.parity=(unsigned int)config->parity;c.stop_bits=config->stop_bits;
    c.special_baud_enabled=(unsigned int)config->special_baud_enabled;c.special_baud=(unsigned int)config->special_baud;
    c.transport=(unsigned int)config->transport;c.endpoint_port=config->endpoint_port;return c;
}

static int transport_start(void *context,const gateway_port_config_t*config)
{gateway_real_adapter_t*a=(gateway_real_adapter_t*)context;size_t n;if(!a||!config||!a->runtime||a->listener_started)return-1;n=strlen(config->bind_address);if(n==0U||n>=sizeof(a->bind_address))return-1;memcpy(a->bind_address,config->bind_address,n+1U);
if(a->builtin_listener){
 if(config->transport==TRANSPORT_MODBUS_TCP){a->listener_driver=gateway_modbus_listener_driver();a->listener_context=&a->listener;}
 else if(config->transport==TRANSPORT_MODBUS_UDP||config->transport==TRANSPORT_RTU_UDP){a->listener_driver=udp_driver(config->transport==TRANSPORT_RTU_UDP);a->listener_context=&a->udp_listener;}
 else if(config->transport==TRANSPORT_RAW_TCP||config->transport==TRANSPORT_RAW_UDP){a->listener_driver=raw_driver(config->transport==TRANSPORT_RAW_UDP);a->listener_context=a;}
 else return-1;
}
a->listener_driver->init(a->listener_context,a->runtime);if(a->listener_driver->open(a->listener_context,a->bind_address,(unsigned short)config->endpoint_port)!=0)return-1;a->listener_started=1;return 0;}
static void transport_step(void *context,core_tick_t now)
{gateway_real_adapter_t*a=(gateway_real_adapter_t*)context;if(a&&a->listener_started)a->listener_driver->step(a->listener_context,now);}
static int transport_stop(void *context)
{gateway_real_adapter_t*a=(gateway_real_adapter_t*)context;int result;if(!a||!a->listener_started)return 0;result=a->listener_driver->close(a->listener_context);if(result==0)a->listener_started=0;return result;}
static unsigned int clients(const void *context)
{const gateway_real_adapter_t*a=(const gateway_real_adapter_t*)context;return a&&a->listener_started?a->listener_driver->clients(a->listener_context):0;}
static unsigned int crc_errors(const void *context)
{const gateway_real_adapter_t*a=(const gateway_real_adapter_t*)context;return a?a->listener_driver->crc_errors(a->listener_context):0;}
static unsigned int framing_errors(const void *context)
{const gateway_real_adapter_t*a=(const gateway_real_adapter_t*)context;return a?a->listener_driver->framing_errors(a->listener_context):0;}
static void diagnostics(const void*context,gateway_transport_diagnostics_t*d)
{const gateway_real_adapter_t*a=(const gateway_real_adapter_t*)context;if(!d)return;memset(d,0,sizeof(*d));if(a&&a->listener_driver&&a->listener_driver->diagnostics)a->listener_driver->diagnostics(a->listener_context,d);}
static const gateway_transport_ops_t transport_ops={transport_start,transport_step,transport_stop,clients,crc_errors,framing_errors,diagnostics};

int gateway_real_adapter_init(gateway_real_adapter_t*a,unsigned int index,const gateway_port_config_t*config,const uart_syscalls_t*sys,void*sys_context,const gateway_listener_driver_t*driver,void*listener_context)
{core_port_config_t c;size_t n;if(!a||!config||!sys||!driver||!driver->init||!driver->open||!driver->step||!driver->close||!driver->clients||!driver->crc_errors||!driver->framing_errors||index>=GATEWAY_PORT_COUNT)return-1;n=strlen(config->bind_address);if(n==0||n>=GATEWAY_BIND_ADDRESS_LENGTH)return-1;memset(a,0,sizeof(*a));c=core_config(config);if(uart_backend_init(&a->uart,index,&c,sys,sys_context)!=0)return-1;uart_backend_set_frame_probe(&a->uart,modbus_rtu_frame_probe,0);memcpy(a->bind_address,config->bind_address,n+1U);a->port_index=index;a->listener_driver=driver;a->listener_context=listener_context?listener_context:&a->listener;a->builtin_listener=driver==gateway_modbus_listener_driver()&&!listener_context;return 0;}
void gateway_real_adapter_attach(gateway_real_adapter_t*a,port_runtime_t*r){if(a)a->runtime=r;}
gateway_port_binding_t gateway_real_adapter_binding(gateway_real_adapter_t*a)
{gateway_port_binding_t b;memset(&b,0,sizeof(b));b.backend_ops=uart_backend_ops();b.backend_context=&a->uart;b.transport_ops=&transport_ops;b.transport_context=a;return b;}

static void real_init(void*c,port_runtime_t*r){modbus_tcp_listener_init((modbus_tcp_listener_t*)c,r);}
static int real_open(void*c,const char*a,unsigned short p){return modbus_tcp_listener_open((modbus_tcp_listener_t*)c,a,p);}
static void real_step(void*c,core_tick_t n){modbus_tcp_listener_step_io((modbus_tcp_listener_t*)c,n);}
static int real_close(void*c){modbus_tcp_listener_close((modbus_tcp_listener_t*)c);return 0;}
static unsigned int real_clients(const void*c){return modbus_tcp_listener_client_count((const modbus_tcp_listener_t*)c);}
static unsigned int real_crc(const void*c){return ((const modbus_tcp_listener_t*)c)->stats.downstream_crc_failures;}
static unsigned int real_framing(const void*c){return ((const modbus_tcp_listener_t*)c)->stats.downstream_framing_failures;}
static void real_diagnostics(const void*c,gateway_transport_diagnostics_t*d)
{const modbus_tcp_listener_t*l=(const modbus_tcp_listener_t*)c;memset(d,0,sizeof(*d));d->clients=modbus_tcp_listener_client_count(l);d->malformed_mbap=l->stats.malformed;d->tx_overflow=l->stats.tx_overflow;d->write_deadline_expired=l->stats.write_deadline_expired;d->gateway_target_no_response=l->stats.gateway_target_no_response;d->gateway_path_unavailable=l->stats.gateway_path_unavailable;d->crc_failures=l->stats.downstream_crc_failures;d->framing_failures=l->stats.downstream_framing_failures;d->unit_mismatches=l->stats.downstream_unit_mismatches;d->function_mismatches=l->stats.downstream_function_mismatches;d->leading_garbage=l->stats.downstream_leading_garbage;}
static const gateway_listener_driver_t real_driver={real_init,real_open,real_step,real_close,real_clients,real_crc,real_framing,real_diagnostics};
const gateway_listener_driver_t *gateway_modbus_listener_driver(void){return &real_driver;}

static void udp_init(void*c,port_runtime_t*r){modbus_udp_listener_init(c,r,0);}
static void rtu_udp_init(void*c,port_runtime_t*r){modbus_udp_listener_init(c,r,1);}
static int udp_open(void*c,const char*a,unsigned short p){return modbus_udp_listener_open(c,a,p);}
static void udp_step(void*c,core_tick_t n){modbus_udp_listener_step_io(c,n);}
static int udp_close(void*c){modbus_udp_listener_close(c);return 0;}
static unsigned int udp_clients(const void*c){return modbus_udp_listener_peer_count(c);}
static unsigned int udp_crc(const void*c){return ((const modbus_udp_listener_t*)c)->stats.crc_errors;}
static unsigned int udp_framing(const void*c){return ((const modbus_udp_listener_t*)c)->stats.malformed;}
static void udp_diagnostics(const void*c,gateway_transport_diagnostics_t*d)
{const modbus_udp_listener_t*l=c;memset(d,0,sizeof(*d));d->clients=udp_clients(c);d->malformed_mbap=l->stats.malformed;d->crc_failures=l->stats.crc_errors;d->framing_failures=l->stats.invalid_responses;d->gateway_target_no_response=l->stats.timeouts;d->gateway_path_unavailable=l->stats.failures;d->tx_overflow=l->stats.send_errors;}
static const gateway_listener_driver_t udp_mbap_driver={udp_init,udp_open,udp_step,udp_close,udp_clients,udp_crc,udp_framing,udp_diagnostics};
static const gateway_listener_driver_t udp_rtu_driver={rtu_udp_init,udp_open,udp_step,udp_close,udp_clients,udp_crc,udp_framing,udp_diagnostics};
static const gateway_listener_driver_t *udp_driver(int rtu){return rtu?&udp_rtu_driver:&udp_mbap_driver;}

static void raw_tcp_init(void*c,port_runtime_t*r){gateway_real_adapter_t*a=c;raw_serial_listener_init(&a->raw_listener,r,&a->uart,0);}
static void raw_udp_init(void*c,port_runtime_t*r){gateway_real_adapter_t*a=c;raw_serial_listener_init(&a->raw_listener,r,&a->uart,1);}
static int raw_open(void*c,const char*a,unsigned short p){return raw_serial_listener_open(&((gateway_real_adapter_t*)c)->raw_listener,a,p);}
static void raw_step(void*c,core_tick_t now){raw_serial_listener_step(&((gateway_real_adapter_t*)c)->raw_listener,now);}
static int raw_close(void*c){return raw_serial_listener_close(&((gateway_real_adapter_t*)c)->raw_listener);}
static unsigned int raw_clients(const void*c){return ((const gateway_real_adapter_t*)c)->raw_listener.owned?1U:0U;}
static unsigned int raw_zero(const void*c){(void)c;return 0;}
static void raw_diagnostics(const void*c,gateway_transport_diagnostics_t*d)
{const raw_serial_listener_t*l=&((const gateway_real_adapter_t*)c)->raw_listener;memset(d,0,sizeof(*d));d->clients=l->owned?1U:0U;d->tx_overflow=l->stats.dropped_datagrams;d->write_deadline_expired=l->stats.stalls;d->gateway_path_unavailable=l->stats.io_errors;d->raw_network_received=l->stats.network_received;d->raw_network_sent=l->stats.network_sent;d->raw_serial_read=l->stats.serial_read;d->raw_serial_written=l->stats.serial_written;d->raw_rejected_peers=l->stats.rejected_peers;d->raw_discarded_serial=l->stats.discarded_serial;}
static const gateway_listener_driver_t raw_tcp_driver={raw_tcp_init,raw_open,raw_step,raw_close,raw_clients,raw_zero,raw_zero,raw_diagnostics};
static const gateway_listener_driver_t raw_udp_driver={raw_udp_init,raw_open,raw_step,raw_close,raw_clients,raw_zero,raw_zero,raw_diagnostics};
static const gateway_listener_driver_t *raw_driver(int udp){return udp?&raw_udp_driver:&raw_tcp_driver;}
