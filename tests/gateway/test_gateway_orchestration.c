#include <stdio.h>
#include <string.h>

#include "core/mock_backend.h"
#include "gateway/gateway_controller.h"
#include "gateway/gateway_orchestration_mock.h"
#include "network/modbus_tcp_listener.h"
#include "uart/uart_backend.h"

static unsigned int checks,failed;
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)

const unsigned char orchestration_port_layout[sizeof(gateway_port_controller_t)]={0};
const unsigned char orchestration_controller_layout[sizeof(gateway_controller_t)]={0};
const unsigned char orchestration_runtime_layout[sizeof(port_runtime_t)]={0};
const unsigned char orchestration_listener_layout[sizeof(modbus_tcp_listener_t)]={0};
const unsigned char orchestration_uart_layout[sizeof(uart_backend_t)]={0};
const unsigned char orchestration_client_layout[sizeof(modbus_tcp_client_t)]={0};

typedef struct fixture {gateway_controller_t gateway;gateway_port_config_t config[8];gateway_port_binding_t binding[8];mock_backend_t backend[8];gateway_transport_mock_t transport[8];} fixture_t;

static void fixture_init(fixture_t*f)
{unsigned int i;memset(f,0,sizeof(*f));gateway_configuration_defaults(f->config);for(i=0;i<8U;++i){mock_backend_init(&f->backend[i]);gateway_transport_mock_init(&f->transport[i]);f->binding[i].backend_ops=mock_backend_ops();f->binding[i].backend_context=&f->backend[i];f->binding[i].transport_ops=gateway_transport_mock_ops();f->binding[i].transport_context=&f->transport[i];}CHECK(gateway_controller_init(&f->gateway,f->config,f->binding)==0);}
static void steps(fixture_t*f,core_tick_t start,unsigned int count)
{unsigned int i;for(i=0;i<count;++i)gateway_controller_step(&f->gateway,start+i);}
static void start_ready(fixture_t*f)
{gateway_controller_start(&f->gateway,0);steps(f,0,4);}
static core_submit_result_t submit(gateway_port_controller_t*p,core_tick_t now)
{unsigned char b=0x55;return port_runtime_submit(&p->runtime,&b,1,1,1,now,100,100,0);}

static void defaults_and_validation(void)
{gateway_port_config_t c[8];gateway_error_t e[8];unsigned int i;gateway_configuration_defaults(c);for(i=0;i<8;++i){CHECK(c[i].uart_index==i);CHECK(c[i].endpoint_port==502U+i);CHECK(c[i].enabled);}
 c[1].endpoint_port=c[0].endpoint_port;CHECK(gateway_configuration_validate(c,e)==2&&e[0]==GATEWAY_ERROR_DUPLICATE_ENDPOINT&&e[1]==GATEWAY_ERROR_DUPLICATE_ENDPOINT);gateway_configuration_defaults(c);c[2].uart_index=1;CHECK(gateway_configuration_validate(c,e)==2&&e[1]==GATEWAY_ERROR_DUPLICATE_UART);gateway_configuration_defaults(c);c[3].endpoint_port=0;CHECK(gateway_configuration_validate(c,e)==1);c[3].endpoint_port=65536U;CHECK(gateway_configuration_validate(c,e)==1);gateway_configuration_defaults(c);c[4].transport=TRANSPORT_RAW_TCP;CHECK(gateway_configuration_validate(c,e)==0);c[4].transport=TRANSPORT_RAW_UDP;CHECK(gateway_configuration_validate(c,e)==0);c[4].transport=TRANSPORT_COUNT;CHECK(gateway_configuration_validate(c,e)==1&&e[4]==GATEWAY_ERROR_UNSUPPORTED_TRANSPORT);gateway_configuration_defaults(c);c[5].data_bits=9;CHECK(gateway_configuration_validate(c,e)==1);c[5].data_bits=8;c[5].uart_index=8;CHECK(gateway_configuration_validate(c,e)==1);}

static void startup_isolation(void)
{fixture_t f;gateway_health_t h;unsigned int i;fixture_init(&f);f.backend[1].open_failures_remaining=10000;f.backend[1].recovery_failures_remaining=10000;f.transport[3].start_failures_remaining=1;gateway_controller_start(&f.gateway,0);steps(&f,0,1100);gateway_controller_health(&f.gateway,&h);for(i=0;i<8;++i)if(i!=1&&i!=3)CHECK(f.gateway.ports[i].lifecycle==GATEWAY_PORT_READY);CHECK(f.gateway.ports[1].lifecycle==GATEWAY_PORT_DEGRADED||f.gateway.ports[1].lifecycle==GATEWAY_PORT_ERROR);CHECK(f.gateway.ports[3].lifecycle==GATEWAY_PORT_ERROR);CHECK(h.ready_ports==6&&h.degraded_or_error_ports==2);CHECK(submit(&f.gateway.ports[7],1200)==CORE_ACCEPTED);steps(&f,1200,4);CHECK(f.gateway.ports[7].runtime.stats.completed==1);}

static void disabled_enable_and_health(void)
{fixture_t f;gateway_port_config_t c;gateway_health_t h;fixture_init(&f);f.config[5].enabled=0;CHECK(gateway_controller_init(&f.gateway,f.config,f.binding)==0);start_ready(&f);CHECK(f.gateway.ports[5].lifecycle==GATEWAY_PORT_DISABLED&&!f.transport[5].active);CHECK(submit(&f.gateway.ports[5],10)==CORE_PORT_DISABLED);c=f.config[5];c.enabled=1;c.revision=2;CHECK(gateway_controller_reconfigure(&f.gateway,5,&c,10)==0);steps(&f,10,4);CHECK(f.gateway.ports[5].lifecycle==GATEWAY_PORT_READY&&f.transport[5].active);f.transport[0].clients=3;f.transport[0].crc_error_count=2;f.transport[0].framing_error_count=4;gateway_controller_health(&f.gateway,&h);CHECK(h.configured_ports==8&&h.enabled_ports==8&&h.ready_ports==8&&h.active_clients==3);CHECK(h.port[0].crc_errors==2&&h.port[0].framing_errors==4);c.enabled=0;c.revision=3;CHECK(gateway_controller_reconfigure(&f.gateway,5,&c,20)==0);steps(&f,20,4);CHECK(f.gateway.ports[5].lifecycle==GATEWAY_PORT_DISABLED&&!f.transport[5].active);}

static void reconfigure_and_rollback(void)
{fixture_t f;gateway_port_config_t c;fixture_init(&f);start_ready(&f);c=f.gateway.ports[6].current;c.endpoint_port=1607;c.revision=2;CHECK(gateway_controller_reconfigure(&f.gateway,6,&c,10)==0);steps(&f,10,4);CHECK(f.gateway.ports[6].lifecycle==GATEWAY_PORT_READY&&f.gateway.ports[6].current.endpoint_port==1607&&f.gateway.ports[6].config_generation==2);
 c.endpoint_port=1707;c.revision=3;f.backend[6].reconfigure_failures_remaining=1;CHECK(gateway_controller_reconfigure(&f.gateway,6,&c,20)==0);steps(&f,20,4);CHECK(f.gateway.ports[6].lifecycle==GATEWAY_PORT_READY&&f.gateway.ports[6].current.endpoint_port==1607&&f.gateway.ports[6].reconfigure_failures==1);CHECK(f.gateway.ports[0].lifecycle==GATEWAY_PORT_READY);
 c.endpoint_port=1807;c.revision=4;f.backend[6].reconfigure_failures_remaining=2;CHECK(gateway_controller_reconfigure(&f.gateway,6,&c,30)==0);steps(&f,30,4);CHECK(f.gateway.ports[6].lifecycle==GATEWAY_PORT_ERROR&&f.gateway.ports[6].rollback_failures==1);CHECK(f.gateway.ports[7].lifecycle==GATEWAY_PORT_READY);}

static void timeout_queue_and_stale(void)
{fixture_t f;gateway_port_config_t c;unsigned int i;fixture_init(&f);start_ready(&f);CHECK(mock_backend_add_plan(&f.backend[2],MOCK_NO_RESPONSE,0)==0);CHECK(submit(&f.gateway.ports[2],10)==CORE_ACCEPTED);steps(&f,10,3);gateway_controller_step(&f.gateway,111);steps(&f,112,10);CHECK(f.gateway.ports[2].runtime.stats.timeouts==1&&f.gateway.ports[2].runtime.stats.recoveries==1);for(i=0;i<CORE_QUEUE_CAPACITY;++i)CHECK(submit(&f.gateway.ports[0],200)==CORE_ACCEPTED);CHECK(submit(&f.gateway.ports[0],200)==CORE_QUEUE_FULL);
 CHECK(mock_backend_add_plan(&f.backend[6],MOCK_LATE_RESPONSE,50)==0);CHECK(submit(&f.gateway.ports[6],300)==CORE_ACCEPTED);steps(&f,300,3);c=f.gateway.ports[6].current;c.endpoint_port=2607;c.revision=2;CHECK(gateway_controller_reconfigure(&f.gateway,6,&c,304)==0);steps(&f,304,4);CHECK(submit(&f.gateway.ports[6],360)==CORE_ACCEPTED);steps(&f,360,4);CHECK(f.gateway.ports[6].runtime.stats.stale_responses>=1U);CHECK(f.gateway.ports[7].lifecycle==GATEWAY_PORT_READY);}

static void fairness_and_soak(void)
{fixture_t f;unsigned int cycle,p;fixture_init(&f);start_ready(&f);for(cycle=0;cycle<12500U;++cycle){core_tick_t now=10U+cycle*4U;for(p=0;p<8U;++p)CHECK(submit(&f.gateway.ports[p],now)==CORE_ACCEPTED);steps(&f,now,4);}for(p=0;p<8U;++p){CHECK(f.gateway.ports[p].runtime.stats.completed==12500U);CHECK(f.gateway.ports[p].scheduler_steps==50004U);CHECK(f.gateway.ports[p].runtime.stats.queue_high_water<=1U);}CHECK(f.gateway.scheduler_steps==50004U);printf("orchestration_soak operations=100000 fairness=%u,%u,%u,%u,%u,%u,%u,%u\n",f.gateway.ports[0].runtime.stats.completed,f.gateway.ports[1].runtime.stats.completed,f.gateway.ports[2].runtime.stats.completed,f.gateway.ports[3].runtime.stats.completed,f.gateway.ports[4].runtime.stats.completed,f.gateway.ports[5].runtime.stats.completed,f.gateway.ports[6].runtime.stats.completed,f.gateway.ports[7].runtime.stats.completed);}

static void shutdown_isolation(void)
{fixture_t f;unsigned int i;fixture_init(&f);start_ready(&f);f.backend[4].stop_failures_remaining=2000;f.transport[3].stop_failures_remaining=1;gateway_controller_shutdown(&f.gateway,10);steps(&f,10,1100);for(i=0;i<8U;++i)if(i!=3&&i!=4)CHECK(f.gateway.ports[i].lifecycle==GATEWAY_PORT_DISABLED);CHECK(f.gateway.ports[3].lifecycle==GATEWAY_PORT_ERROR&&f.gateway.ports[3].stop_failures==1);CHECK(f.gateway.ports[4].lifecycle==GATEWAY_PORT_ERROR);}

static void endpoint_families(void)
{
    gateway_port_config_t c[GATEWAY_PORT_COUNT];gateway_error_t e[GATEWAY_PORT_COUNT];unsigned a,b,i;
    for(a=0;a<TRANSPORT_COUNT;++a)for(b=0;b<TRANSPORT_COUNT;++b){
        int tcp_a=a==TRANSPORT_MODBUS_TCP||a==TRANSPORT_RAW_TCP;
        int tcp_b=b==TRANSPORT_MODBUS_TCP||b==TRANSPORT_RAW_TCP;
        gateway_configuration_defaults(c);for(i=2;i<GATEWAY_PORT_COUNT;++i)c[i].enabled=0;
        c[0].transport=(transport_type_t)a;c[1].transport=(transport_type_t)b;c[1].endpoint_port=c[0].endpoint_port;
        CHECK(gateway_configuration_validate(c,e)==(tcp_a==tcp_b?2:0));
        if(tcp_a==tcp_b)CHECK(e[0]==GATEWAY_ERROR_DUPLICATE_ENDPOINT&&e[1]==GATEWAY_ERROR_DUPLICATE_ENDPOINT);
    }
}

/* Listener-only changes retain the UART and cancel old callbacks before
 * replacing their transport context, including a pending late response. */
static void listener_rebind(void)
{
    fixture_t f;gateway_port_config_t c;unsigned int opens,stops,reconfigures;
    fixture_init(&f);start_ready(&f);
    opens=f.backend[0].opens;stops=f.backend[0].stops;reconfigures=f.backend[0].reconfigures;
    c=f.gateway.ports[0].current;strcpy(c.bind_address,"192.0.2.10");++c.revision;
    CHECK(gateway_controller_reconfigure(&f.gateway,0,&c,10)==0);steps(&f,10,4);
    CHECK(!strcmp(f.gateway.ports[0].current.bind_address,"192.0.2.10"));
    CHECK(f.backend[0].opens==opens&&f.backend[0].stops==stops&&f.backend[0].reconfigures==reconfigures);
    CHECK(f.transport[1].starts==1&&f.transport[1].stops==0);
    CHECK(mock_backend_add_plan(&f.backend[0],MOCK_LATE_RESPONSE,50)==0);
    CHECK(submit(&f.gateway.ports[0],20)==CORE_ACCEPTED);steps(&f,20,3);
    CHECK(submit(&f.gateway.ports[0],23)==CORE_ACCEPTED);
    strcpy(c.bind_address,"192.0.2.11");++c.revision;
    CHECK(gateway_controller_reconfigure(&f.gateway,0,&c,24)==0);
    CHECK(!f.gateway.ports[0].runtime.has_active&&!f.gateway.ports[0].runtime.queue.count);
    CHECK(f.gateway.ports[0].runtime.stats.cancelled==2);
    CHECK(f.gateway.ports[0].lifecycle==GATEWAY_PORT_RECONFIGURING);
    steps(&f,24,100);
    CHECK(f.gateway.ports[0].lifecycle==GATEWAY_PORT_READY);
    CHECK(f.backend[0].opens==opens&&f.backend[0].stops==stops&&f.backend[0].reconfigures==reconfigures);
    strcpy(c.bind_address,"192.0.2.12");++c.revision;f.transport[0].start_failures_remaining=1;
    CHECK(gateway_controller_reconfigure(&f.gateway,0,&c,130)==0);steps(&f,130,4);
    CHECK(f.gateway.ports[0].lifecycle==GATEWAY_PORT_READY);
    CHECK(!strcmp(f.gateway.ports[0].current.bind_address,"192.0.2.11"));
    CHECK(f.backend[0].reconfigures==reconfigures);
}

static void lease_loss_during_uart_change(void)
{
 fixture_t f;gateway_port_config_t c;unsigned int starts;
 fixture_init(&f);start_ready(&f);c=f.gateway.ports[0].current;c.baud=19200;++c.revision;
 starts=f.transport[0].starts;CHECK(!gateway_controller_reconfigure(&f.gateway,0,&c,10));
 CHECK(!gateway_controller_network_wait(&f.gateway,0,1,10));steps(&f,10,10);
 CHECK(f.gateway.ports[0].lifecycle==GATEWAY_PORT_NETWORK_WAIT&&!f.transport[0].active);
 CHECK(f.transport[0].starts==starts&&f.gateway.ports[0].current.baud==19200);
 CHECK(f.gateway.ports[1].lifecycle==GATEWAY_PORT_READY&&f.transport[1].starts==1);
}

int main(void)
{lease_loss_during_uart_change();listener_rebind();endpoint_families();defaults_and_validation();startup_isolation();disabled_enable_and_health();reconfigure_and_rollback();timeout_queue_and_stale();fairness_and_soak();shutdown_isolation();printf("orchestration checks=%u failed=%u port=%u controller=%u runtime=%u listener=%u uart=%u client=%u\n",checks,failed,(unsigned)sizeof(gateway_port_controller_t),(unsigned)sizeof(gateway_controller_t),(unsigned)sizeof(port_runtime_t),(unsigned)sizeof(modbus_tcp_listener_t),(unsigned)sizeof(uart_backend_t),(unsigned)sizeof(modbus_tcp_client_t));return failed?1:0;}
