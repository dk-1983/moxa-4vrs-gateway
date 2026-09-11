#include <stdio.h>
#include <string.h>

#include "app/gateway_application.h"
#include "core/mock_backend.h"
#include "gateway/gateway_orchestration_mock.h"

static unsigned int checks,failed;
const unsigned char application_layout[sizeof(gateway_application_t)]={0};
const unsigned char application_health_layout[sizeof(gateway_application_health_t)]={0};
const unsigned char real_adapters_layout[sizeof(gateway_real_adapter_t)*GATEWAY_PORT_COUNT]={0};
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)

typedef struct fixture {
    gateway_application_t application;
    gateway_application_dependencies_t dependencies;
    gateway_persistent_config_t config;
    gateway_config_result_t load_result;
    gateway_config_source_t source;
    gateway_port_binding_t bindings[GATEWAY_PORT_COUNT];
    mock_backend_t backend[GATEWAY_PORT_COUNT];
    gateway_transport_mock_t transport[GATEWAY_PORT_COUNT];
    core_tick_t now;
    unsigned int load_calls,provide_calls,attach_calls,platform_calls,wait_calls;
    gateway_persistent_config_t staged;
    gateway_config_result_t stage_result,promote_result;
    unsigned int stage_calls,promote_calls,discard_calls;
    gateway_system_time_t system_time;
    gateway_system_time_result_t time_set_result;
    unsigned int time_get_calls,time_set_calls;
    int ntp_start_result,ntp_poll_result;
    unsigned int ntp_start_calls,ntp_poll_calls,ntp_cancel_calls;
} fixture_t;

static core_tick_t clock_now(void*c){return((fixture_t*)c)->now;}
static void wait_now(void*c,unsigned int us){fixture_t*f=(fixture_t*)c;(void)us;++f->wait_calls;++f->now;}
static int platform(void*c,gateway_platform_info_t*i){fixture_t*f=(fixture_t*)c;++f->platform_calls;memset(i,0,sizeof(*i));strcpy(i->model,"test-platform");strcpy(i->kernel,"test-kernel");strcpy(i->architecture,"test-arch");i->word_bits=32;i->big_endian=1;return 0;}
static gateway_config_result_t select_config(void*c,const char*d,gateway_persistent_config_t*p,gateway_config_source_t*s){fixture_t*f=(fixture_t*)c;(void)d;++f->load_calls;if(f->load_result==GATEWAY_CONFIG_OK||f->load_result==GATEWAY_CONFIG_ABSENT){*p=f->config;*s=f->source;}return f->load_result;}
static int provide(void*c,const gateway_port_config_t*p,gateway_port_binding_t*b){fixture_t*f=(fixture_t*)c;(void)p;++f->provide_calls;memcpy(b,f->bindings,sizeof(f->bindings));return 0;}
static int attach(void*c,gateway_controller_t*controller){fixture_t*f=(fixture_t*)c;(void)controller;++f->attach_calls;return 0;}
static gateway_config_result_t stage_config(void*c,const char*d,const gateway_persistent_config_t*p){fixture_t*f=(fixture_t*)c;(void)d;++f->stage_calls;f->staged=*p;return f->stage_result;}
static gateway_config_result_t promote_config(void*c,const char*d){fixture_t*f=(fixture_t*)c;(void)d;++f->promote_calls;if(f->promote_result==GATEWAY_CONFIG_OK||f->promote_result==GATEWAY_CONFIG_DURABILITY_UNCERTAIN)f->config=f->staged;return f->promote_result;}
static void discard_config(void*c,const char*d){fixture_t*f=(fixture_t*)c;(void)d;++f->discard_calls;}
static gateway_system_time_result_t get_system_time(void*c,gateway_system_time_t*v){fixture_t*f=(fixture_t*)c;++f->time_get_calls;*v=f->system_time;return GATEWAY_SYSTEM_TIME_OK;}
static gateway_system_time_result_t set_system_time(void*c,const gateway_system_time_t*v){fixture_t*f=(fixture_t*)c;++f->time_set_calls;f->system_time=*v;return f->time_set_result;}
static int ntp_start(void*c,const char*s){fixture_t*f=(fixture_t*)c;(void)s;++f->ntp_start_calls;return f->ntp_start_result;}
static int ntp_poll(void*c){fixture_t*f=(fixture_t*)c;++f->ntp_poll_calls;return f->ntp_poll_result;}
static void ntp_cancel(void*c){++((fixture_t*)c)->ntp_cancel_calls;}
static void prepare(fixture_t*f,gateway_config_result_t result,gateway_config_source_t source)
{unsigned int i;memset(f,0,sizeof(*f));gateway_persistent_defaults(&f->config);f->load_result=result;f->source=source;f->stage_result=GATEWAY_CONFIG_OK;f->promote_result=GATEWAY_CONFIG_OK;f->time_set_result=GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED;f->ntp_poll_result=0;f->system_time=(gateway_system_time_t){2026U,9U,5U,12U,30U,0U};f->now=10;for(i=0;i<GATEWAY_PORT_COUNT;++i){mock_backend_init(&f->backend[i]);gateway_transport_mock_init(&f->transport[i]);f->bindings[i].backend_ops=mock_backend_ops();f->bindings[i].backend_context=&f->backend[i];f->bindings[i].transport_ops=gateway_transport_mock_ops();f->bindings[i].transport_context=&f->transport[i];}gateway_application_dependencies_default(&f->dependencies);f->dependencies.configuration_directory="test-config";f->dependencies.select_configuration=select_config;f->dependencies.select_context=f;f->dependencies.provide_bindings=provide;f->dependencies.attach_bindings=attach;f->dependencies.binding_context=f;f->dependencies.platform_provider=platform;f->dependencies.platform_context=f;f->dependencies.clock=clock_now;f->dependencies.clock_context=f;f->dependencies.wait=wait_now;f->dependencies.wait_context=f;f->dependencies.system_time_get=get_system_time;f->dependencies.system_time_set=set_system_time;f->dependencies.system_time_context=f;f->dependencies.ntp_start=ntp_start;f->dependencies.ntp_poll=ntp_poll;f->dependencies.ntp_cancel=ntp_cancel;f->dependencies.ntp_context=f;f->dependencies.stage_configuration=stage_config;f->dependencies.promote_configuration=promote_config;f->dependencies.discard_configuration=discard_config;f->dependencies.configuration_write_context=f;CHECK(gateway_application_init(&f->application,&f->dependencies)==0);}
static void step_to_runtime(fixture_t*f){unsigned int i;for(i=0;i<3000U&&f->application.process_state==GATEWAY_PROCESS_STARTING;++i){gateway_application_step(&f->application);++f->now;}}
static void step_to_stop(fixture_t*f){unsigned int i;for(i=0;i<3000U&&!gateway_application_finished(&f->application);++i){gateway_application_step(&f->application);++f->now;}}

static void ready_and_shutdown(void)
{fixture_t f;gateway_application_health_t h;unsigned int i;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);CHECK(f.application.coordinator.state==GATEWAY_APP_READY&&f.application.process_state==GATEWAY_PROCESS_RUNNING);CHECK(f.load_calls==1&&f.provide_calls==1&&f.attach_calls==1&&f.platform_calls==1);gateway_application_health(&f.application,&h);CHECK(strcmp(h.product.name,"4VRS Gateway")==0&&strcmp(h.product.version,"v2026.02.01")==0);CHECK(strcmp(h.configuration_directory,"test-config")==0&&h.coordinator.ready_ports==8);gateway_application_request_stop(&f.application);gateway_application_request_stop(&f.application);step_to_stop(&f);CHECK(f.application.exit_status==GATEWAY_EXIT_CLEAN&&f.application.cleanup_attempts==1&&f.application.shutdown_requests==2);for(i=0;i<8U;++i)CHECK(f.backend[i].stops==1&&f.transport[i].stops==1);}
static void degraded(void)
{fixture_t f;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);f.backend[2].open_failures_remaining=10000;f.backend[2].recovery_failures_remaining=10000;step_to_runtime(&f);CHECK(f.application.coordinator.state==GATEWAY_APP_DEGRADED&&f.application.process_state==GATEWAY_PROCESS_RUNNING);gateway_application_request_stop(&f.application);step_to_stop(&f);CHECK(f.application.exit_status==GATEWAY_EXIT_CLEAN);}
static void safe_mode(void)
{fixture_t f;unsigned int i;prepare(&f,GATEWAY_CONFIG_INVALID,GATEWAY_CONFIG_SOURCE_SAFE_MODE);step_to_runtime(&f);CHECK(f.application.coordinator.state==GATEWAY_APP_SAFE_MODE&&f.application.process_state==GATEWAY_PROCESS_RUNNING);CHECK(f.provide_calls==1&&f.attach_calls==1);for(i=0;i<8U;++i)CHECK(f.backend[i].opens==0&&f.transport[i].starts==0);gateway_application_request_stop(&f.application);step_to_stop(&f);CHECK(f.application.exit_status==GATEWAY_EXIT_SAFE_MODE&&f.application.coordinator.cleanup_complete);}
static int fail_init(gateway_controller_t*c,const gateway_port_config_t*p,const gateway_port_binding_t*b){(void)c;(void)p;(void)b;return-1;}
static const gateway_controller_driver_t fatal_driver={fail_init,gateway_controller_start,gateway_controller_step,gateway_controller_shutdown,gateway_controller_health};
static void fatal_and_early_stop(void)
{fixture_t f;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);f.application.coordinator.controller_driver=&fatal_driver;step_to_runtime(&f);step_to_stop(&f);CHECK(f.application.exit_status==GATEWAY_EXIT_FATAL_STARTUP&&f.application.cleanup_attempts==1);prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);gateway_application_request_stop(&f.application);step_to_stop(&f);CHECK(f.application.exit_status==GATEWAY_EXIT_CLEAN&&f.application.coordinator.controller_initialized==0&&f.application.cleanup_attempts==1);}
static void cleanup_failure(void)
{fixture_t f;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);f.transport[0].stop_failures_remaining=1;gateway_application_request_stop(&f.application);step_to_stop(&f);CHECK(f.application.exit_status==GATEWAY_EXIT_CLEANUP_FAILURE&&!f.application.coordinator.cleanup_complete);}
static void metadata_and_bounds(void)
{gateway_application_dependencies_t d;gateway_platform_info_t p;gateway_application_dependencies_default(&d);CHECK(strcmp(d.configuration_directory,GATEWAY_CONFIG_TARGET_DIRECTORY)==0);CHECK(gateway_platform_info_default(0,&p)==0&&p.word_bits==sizeof(void*)*8U);CHECK(gateway_application_memory_bytes()==sizeof(gateway_application_t));CHECK(gateway_application_adapter_bytes()==sizeof(gateway_real_adapter_t)*8U);CHECK(gateway_application_max_fds()==89U);CHECK(gateway_application_init(0,&d)!=0);}
static void production_binding_assembly(void)
{gateway_application_t a;gateway_application_dependencies_t d;gateway_persistent_config_t p;gateway_port_binding_t b[8];gateway_controller_t c;unsigned int i;memset(&a,0,sizeof(a));memset(b,0,sizeof(b));gateway_persistent_defaults(&p);for(i=1;i<8U;++i)p.ports[i].enabled=0;p.ports[0].uart_index=0;p.ports[0].endpoint_port=1502;strcpy(p.ports[0].bind_address,"127.0.0.1");gateway_application_dependencies_production(&d,&a,"host-test");CHECK(strcmp(d.configuration_directory,"host-test")==0);CHECK(d.provide_bindings(d.binding_context,p.ports,b)==0&&a.prepared_adapters==0xffU);CHECK(strcmp(a.adapters[0].uart.device_path,"/dev/ttyM0")==0&&a.adapters[0].port_index==0);CHECK(strcmp(a.adapters[1].uart.device_path,"/dev/ttyM1")==0&&b[1].backend_ops!=0&&b[1].transport_ops!=0);CHECK(gateway_controller_init(&c,p.ports,b)==0);CHECK(d.attach_bindings(d.binding_context,&c)==0&&a.adapters[0].runtime==&c.ports[0].runtime);}

static void configuration_transactions(void)
{fixture_t f;gateway_persistent_config_t c;unsigned int i;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);c=f.config;c.ports[2].endpoint_port=1604;c.ports[2].revision=2;CHECK(gateway_application_request_configuration(&f.application,&c)==0);for(i=0;i<100U&&gateway_application_configuration_state(&f.application)<GATEWAY_CONFIG_TX_SUCCEEDED;++i){gateway_application_step(&f.application);++f.now;}CHECK(gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_SUCCEEDED);CHECK(f.stage_calls==1&&f.promote_calls==1&&f.application.coordinator.selected.ports[2].endpoint_port==1604);
 CHECK(gateway_application_request_configuration(&f.application,&c)==0&&gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_UNCHANGED);
 c.ports[3].endpoint_port=1605;c.ports[3].revision=2;f.stage_result=GATEWAY_CONFIG_IO_ERROR;CHECK(gateway_application_request_configuration(&f.application,&c)!=0&&gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_FAILED);
 f.stage_result=GATEWAY_CONFIG_OK;f.promote_result=GATEWAY_CONFIG_DURABILITY_UNCERTAIN;CHECK(gateway_application_request_configuration(&f.application,&c)==0);for(i=0;i<100U&&gateway_application_configuration_state(&f.application)!=GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN;++i){gateway_application_step(&f.application);++f.now;}CHECK(gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN&&f.application.coordinator.selected.ports[3].endpoint_port==1605);
 c.ports[4].endpoint_port=1606;c.ports[4].revision=2;f.promote_result=GATEWAY_CONFIG_IO_ERROR;CHECK(gateway_application_request_configuration(&f.application,&c)==0);for(i=0;i<200U&&gateway_application_configuration_state(&f.application)!=GATEWAY_CONFIG_TX_FAILED;++i){gateway_application_step(&f.application);++f.now;}CHECK(gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_FAILED&&f.discard_calls>=1U&&f.application.coordinator.selected.ports[4].endpoint_port==506);
 c=f.application.coordinator.selected;c.ports[0].uart_index=c.ports[1].uart_index;CHECK(gateway_application_request_configuration(&f.application,&c)!=0&&f.application.configuration_transaction.validation_error==GATEWAY_ERROR_DUPLICATE_UART);}

static void runtime_uart_identity_is_fixed(void)
{fixture_t f;gateway_persistent_config_t c;gateway_error_t errors[8];unsigned int u;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);c=f.application.coordinator.selected;u=c.ports[0].uart_index;c.ports[0].uart_index=c.ports[1].uart_index;c.ports[1].uart_index=u;CHECK(gateway_configuration_validate(c.ports,errors)==0);CHECK(gateway_application_request_configuration(&f.application,&c)!=0);CHECK(f.application.configuration_transaction.validation_error==GATEWAY_ERROR_INVALID_CONFIG);CHECK(f.stage_calls==0);}

static void activation_failure_rolls_back(void)
{fixture_t f;gateway_persistent_config_t c;unsigned int i;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);c=f.application.coordinator.selected;c.ports[2].baud=19200UL;c.ports[2].revision=2;f.backend[2].reconfigure_failures_remaining=1;CHECK(gateway_application_request_configuration(&f.application,&c)==0);for(i=0;i<200U&&gateway_application_configuration_state(&f.application)!=GATEWAY_CONFIG_TX_FAILED;++i){gateway_application_step(&f.application);++f.now;}CHECK(gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_FAILED);CHECK(f.promote_calls==0&&f.discard_calls==1);CHECK(f.application.coordinator.selected.ports[2].baud==9600UL);CHECK(f.application.coordinator.controller.ports[2].current.baud==9600UL);}

static void shutdown_cancels_unpromoted_candidate(void)
{fixture_t f;gateway_persistent_config_t c;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);c=f.application.coordinator.selected;c.ports[2].baud=19200UL;c.ports[2].revision=2;CHECK(gateway_application_request_configuration(&f.application,&c)==0);gateway_application_request_stop(&f.application);gateway_application_step(&f.application);CHECK(gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_FAILED);CHECK(f.discard_calls==1&&f.promote_calls==0);step_to_stop(&f);}

static void system_time_api_and_monotonic_separation(void)
{fixture_t f;gateway_system_time_t v;core_tick_t monotonic;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);CHECK(gateway_application_system_time_get(&f.application,&v)==GATEWAY_SYSTEM_TIME_OK&&v.year==2026U&&f.time_get_calls==1U);monotonic=f.now;v=(gateway_system_time_t){2024U,2U,29U,23U,59U,59U};CHECK(gateway_system_time_validate(&v)==0);CHECK(gateway_application_system_time_set(&f.application,&v)==GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED&&f.time_set_calls==1U);CHECK(f.now==monotonic&&f.application.process_state==GATEWAY_PROCESS_RUNNING);v.year=2025U;CHECK(gateway_system_time_validate(&v)!=0);v=(gateway_system_time_t){2026U,13U,1U,0U,0U,0U};CHECK(gateway_system_time_validate(&v)!=0);v=(gateway_system_time_t){2026U,4U,31U,0U,0U,0U};CHECK(gateway_system_time_validate(&v)!=0);v=(gateway_system_time_t){2026U,12U,31U,24U,0U,0U};CHECK(gateway_system_time_validate(&v)!=0);v=(gateway_system_time_t){2026U,12U,31U,23U,60U,0U};CHECK(gateway_system_time_validate(&v)!=0);v=(gateway_system_time_t){1999U,12U,31U,23U,59U,59U};CHECK(gateway_system_time_validate(&v)!=0);f.time_set_result=GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED;v=(gateway_system_time_t){2026U,9U,5U,12U,0U,0U};CHECK(gateway_application_system_time_set(&f.application,&v)==GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED&&f.application.process_state==GATEWAY_PROCESS_RUNNING);}

static void ntp_state_retry_timeout_and_manual_transition(void)
{fixture_t f;gateway_system_time_t v;core_tick_t retry;
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");CHECK(f.application.time.trust==GATEWAY_TIME_UNSYNCED&&f.ntp_start_calls==0U);gateway_application_step(&f.application);CHECK(f.ntp_start_calls==0U);step_to_runtime(&f);CHECK(f.application.process_state==GATEWAY_PROCESS_RUNNING&&f.application.time.trust==GATEWAY_TIME_SYNCING&&f.ntp_start_calls==1U);f.ntp_poll_result=1;gateway_application_step(&f.application);CHECK(f.application.time.trust==GATEWAY_TIME_SYNCED&&f.application.time.last_ntp_result==GATEWAY_NTP_SUCCEEDED&&f.application.time.last_sync==f.now);
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);f.ntp_poll_result=-1;gateway_application_step(&f.application);CHECK(f.application.time.trust==GATEWAY_TIME_ERROR&&f.application.process_state==GATEWAY_PROCESS_RUNNING);retry=f.application.time.next_ntp_attempt;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==1U);f.now=retry-1U;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==1U);f.now=retry;f.ntp_poll_result=0;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==2U&&f.application.time.trust==GATEWAY_TIME_SYNCING);
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);f.now=f.application.ntp_deadline.at;gateway_application_step(&f.application);CHECK(f.application.time.last_ntp_result==GATEWAY_NTP_TIMED_OUT&&f.ntp_cancel_calls==1U&&f.application.process_state==GATEWAY_PROCESS_RUNNING);
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);v=(gateway_system_time_t){2026U,9U,5U,12U,0U,0U};CHECK(gateway_application_system_time_set(&f.application,&v)==GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED&&f.application.time.trust==GATEWAY_TIME_MANUAL);f.application.coordinator.selected.settings.ntp_enabled=1;strcpy(f.application.coordinator.selected.settings.ntp_server,"ntp.test");gateway_application_step(&f.application);CHECK(f.application.time.trust==GATEWAY_TIME_SYNCING);f.ntp_poll_result=1;gateway_application_step(&f.application);CHECK(f.application.time.trust==GATEWAY_TIME_SYNCED);}

static void persistent_ntp_policy_transaction(void)
{fixture_t f;gateway_persistent_config_t c;unsigned int i,opens=0;prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);c=f.application.coordinator.selected;c.settings.ntp_enabled=1;strcpy(c.settings.ntp_server,"10.0.0.10");CHECK(gateway_application_request_configuration(&f.application,&c)==0);for(i=0;i<40U&&gateway_application_configuration_state(&f.application)!=GATEWAY_CONFIG_TX_SUCCEEDED;++i){gateway_application_step(&f.application);++f.now;}CHECK(gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_SUCCEEDED&&f.stage_calls==1U&&f.promote_calls==1U);CHECK(f.application.time.ntp_enabled==1U&&strcmp(f.application.time.ntp_server,"10.0.0.10")==0&&f.ntp_start_calls==1U);for(i=0;i<GATEWAY_PORT_COUNT;++i)opens+=f.backend[i].opens;CHECK(opens==GATEWAY_PORT_COUNT&&f.application.process_state==GATEWAY_PROCESS_RUNNING);c=f.application.coordinator.selected;strcpy(c.settings.ntp_server,"10.0.0.20");CHECK(gateway_application_request_configuration(&f.application,&c)==0);}

static unsigned int rtc_probes,rtc_writes;
static gateway_rtc_state_t injected_rtc;
static gateway_rtc_state_t fake_rtc_probe(void *c){(void)c;++rtc_probes;return injected_rtc;}
static gateway_rtc_state_t fake_rtc_save(void *c){(void)c;++rtc_writes;return injected_rtc;}
static void rtc_and_periodic_ntp(void)
{
 fixture_t f;gateway_system_time_t v={2026U,9U,5U,12U,0U,0U};core_tick_t next;
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 rtc_probes=rtc_writes=0;injected_rtc=GATEWAY_RTC_UNVERIFIED;
 f.application.dependencies.rtc_probe=fake_rtc_probe;f.application.dependencies.rtc_save=fake_rtc_save;
 step_to_runtime(&f);CHECK(rtc_probes==1U&&rtc_writes==0U&&f.application.time.trust==GATEWAY_TIME_UNSYNCED);
 CHECK(f.application.time.rtc==GATEWAY_RTC_UNVERIFIED);
 injected_rtc=GATEWAY_RTC_SAVED;
 CHECK(gateway_application_system_time_set(&f.application,&v)==GATEWAY_SYSTEM_TIME_OK);
 CHECK(rtc_writes==1U&&f.application.time.trust==GATEWAY_TIME_MANUAL&&f.application.time.last_rtc_save==f.now);
 f.application.coordinator.selected.settings.ntp_enabled=1;strcpy(f.application.coordinator.selected.settings.ntp_server,"ntp.test");
 gateway_application_step(&f.application);
 CHECK(gateway_application_system_time_set(&f.application,&v)==GATEWAY_SYSTEM_TIME_SET_FAILED&&f.time_set_calls==1U);
 f.ntp_poll_result=1;gateway_application_step(&f.application);
 CHECK(f.application.time.trust==GATEWAY_TIME_SYNCED&&rtc_writes==2U);
 next=f.application.time.next_ntp_attempt;
 f.now=next-1U;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==1U);
 f.now=next;f.ntp_poll_result=0;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==2U);
 f.ntp_poll_result=-1;gateway_application_step(&f.application);
 CHECK(f.application.time.trust==GATEWAY_TIME_HOLDOVER&&rtc_writes==2U&&f.application.time.ntp_failures==1U);
 CHECK(f.application.time.next_ntp_attempt==f.now+GATEWAY_NTP_RETRY_INITIAL_MS);
 f.now=f.application.time.next_ntp_attempt;gateway_application_step(&f.application);
 injected_rtc=GATEWAY_RTC_WRITE_FAILED;f.ntp_poll_result=1;gateway_application_step(&f.application);
 CHECK(f.application.time.trust==GATEWAY_TIME_SYNCED&&f.application.time.rtc==GATEWAY_RTC_WRITE_FAILED);
 CHECK(f.application.time.rtc_save_failures==1U&&f.application.time.ntp_failures==0U);
 f.application.coordinator.selected.settings.ntp_enabled=0;gateway_application_step(&f.application);
 CHECK(f.application.time.trust==GATEWAY_TIME_HOLDOVER);
 CHECK(gateway_application_system_time_set(&f.application,&v)==GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED);
 CHECK(f.application.time.trust==GATEWAY_TIME_MANUAL);
 gateway_application_request_stop(&f.application);step_to_stop(&f);CHECK(rtc_writes==4U);
 CHECK(gateway_application_system_time_set(&f.application,&v)==GATEWAY_SYSTEM_TIME_SET_FAILED&&rtc_writes==4U);
 /* Calendar validation gates NTP success; invalid time must not reach RTC. */
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.application.dependencies.rtc_save=fake_rtc_save;rtc_writes=0;
 f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 f.system_time.year=1999U;f.ntp_poll_result=1;gateway_application_step(&f.application);
 CHECK(f.application.time.trust==GATEWAY_TIME_ERROR&&rtc_writes==0U);
 /* Hourly deadlines remain correct across the 32-bit monotonic wrap. */
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 f.now=0xfffffff0U;f.ntp_poll_result=1;gateway_application_step(&f.application);
 next=f.application.time.next_ntp_attempt;CHECK(next==0x0036ee70U);
 f.now=next-1U;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==1U);
 f.now=next;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==2U);
 /* Disabled policy still reaps an in-flight helper; no RTC write on cancel. */
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 f.application.coordinator.selected.settings.ntp_enabled=0;gateway_application_step(&f.application);
 CHECK(f.ntp_cancel_calls==1U&&f.application.ntp_terminating);
 f.ntp_poll_result=-1;gateway_application_step(&f.application);
 CHECK(!f.application.ntp_terminating&&f.ntp_start_calls==1U);
 /* Stop waits for helper reap, and never starts a replacement. */
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 gateway_application_request_stop(&f.application);step_to_stop(&f);
 CHECK(!gateway_application_finished(&f.application)&&f.ntp_cancel_calls==1U);
 f.ntp_poll_result=-1;step_to_stop(&f);
 CHECK(gateway_application_finished(&f.application)&&f.ntp_start_calls==1U);
 /* A calendar probe alone cannot establish time trust. */
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 injected_rtc=GATEWAY_RTC_INVALID;f.application.dependencies.rtc_probe=fake_rtc_probe;step_to_runtime(&f);
 CHECK(f.application.time.rtc==GATEWAY_RTC_INVALID&&f.application.time.trust==GATEWAY_TIME_UNSYNCED);
 /* Success from a canceled helper is discarded, including RTC persistence. */
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.application.dependencies.rtc_save=fake_rtc_save;rtc_writes=0;
 f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 f.application.coordinator.selected.settings.ntp_enabled=0;f.ntp_poll_result=1;gateway_application_step(&f.application);
 CHECK(!f.application.ntp_terminating&&rtc_writes==0U&&f.application.time.trust==GATEWAY_TIME_UNSYNCED);
 /* Invalid calendar after a previous sync is not trustworthy holdover. */
 f.application.coordinator.selected.settings.ntp_enabled=1;gateway_application_step(&f.application);
 injected_rtc=GATEWAY_RTC_SAVED;gateway_application_step(&f.application);
 CHECK(f.application.time.trust==GATEWAY_TIME_SYNCED&&rtc_writes==1U);
 f.now=f.application.time.next_ntp_attempt;gateway_application_step(&f.application);
 f.system_time.month=13U;gateway_application_step(&f.application);
 CHECK(f.application.time.trust==GATEWAY_TIME_ERROR&&rtc_writes==1U);
 f.now=f.application.time.next_ntp_attempt;f.ntp_start_result=-1;gateway_application_step(&f.application);
 CHECK(f.application.time.ntp_failures==2U&&f.application.time.next_ntp_attempt==f.now+GATEWAY_NTP_RETRY_SECOND_MS);
 f.now=f.application.time.next_ntp_attempt;gateway_application_step(&f.application);
 CHECK(f.application.time.ntp_failures==3U&&f.application.time.next_ntp_attempt==f.now+GATEWAY_NTP_RETRY_MAX_MS);
}

static void ntp_interval_and_bounded_test(void)
{
 fixture_t f;unsigned int i;core_tick_t saved;
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 f.ntp_poll_result=1;gateway_application_step(&f.application);saved=f.application.time.last_sync;
 f.application.coordinator.selected.settings.ntp_interval_hours=24U;gateway_application_step(&f.application);
 CHECK(f.application.time.next_ntp_attempt==saved+86400000U&&f.ntp_start_calls==1U);
 CHECK(gateway_application_ntp_test(&f.application,1U)==0);
 CHECK(f.application.time.next_ntp_attempt==f.now+60000U);
 for(i=0;i<3U;++i){
  f.now=f.application.time.next_ntp_attempt-1U;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==i+1U);
  ++f.now;f.ntp_poll_result=0;gateway_application_step(&f.application);CHECK(f.application.time.ntp_test_attempts==i+1U);
  f.ntp_poll_result=1;gateway_application_step(&f.application);
 }
 CHECK(!f.application.time.ntp_test_active&&f.application.time.next_ntp_attempt==f.now+86400000U);
 CHECK(f.application.coordinator.selected.settings.ntp_interval_hours==24U);
 CHECK(gateway_application_ntp_test(&f.application,1U)==0);
 for(i=0;i<3U;++i){f.now=f.application.time.next_ntp_attempt;f.ntp_poll_result=0;gateway_application_step(&f.application);f.ntp_poll_result=-1;gateway_application_step(&f.application);}
 CHECK(!f.application.time.ntp_test_active&&f.application.time.ntp_test_attempts==3U);
 CHECK(f.application.time.trust==GATEWAY_TIME_HOLDOVER);
 CHECK(gateway_application_ntp_test(&f.application,1U)==0);
 f.application.coordinator.selected.settings.ntp_enabled=0;gateway_application_step(&f.application);
 CHECK(!f.application.time.ntp_test_active&&gateway_application_ntp_test(&f.application,1U)!=0);
}
static void ntp_test_boundaries(void)
{
 fixture_t f;core_tick_t normal,at;unsigned int i,mode,starts;gateway_persistent_config_t unchanged;
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 /* The ordinary in-flight helper is not a diagnostic attempt. */
 unchanged=f.application.coordinator.selected;
 CHECK(gateway_application_ntp_test(&f.application,1)==0);
 CHECK(f.application.time.ntp_test_attempts==0U&&f.ntp_cancel_calls==0U);
 f.ntp_poll_result=1;gateway_application_step(&f.application);
 CHECK(f.application.time.ntp_test_attempts==0U&&f.application.time.next_ntp_attempt==f.now+60000U);
 normal=f.application.time.last_sync+3600000U;f.now+=1000U;
 CHECK(gateway_application_ntp_test(&f.application,0)==0&&f.application.time.next_ntp_attempt==normal);
 /* Preserve backoff on early stop; interval-only changes do not cancel it. */
 f.now=normal;f.ntp_poll_result=0;gateway_application_step(&f.application);
 f.ntp_poll_result=-1;gateway_application_step(&f.application);normal=f.application.time.next_ntp_attempt;
 f.application.coordinator.selected.settings.ntp_interval_hours=6U;gateway_application_step(&f.application);
 CHECK(f.application.time.next_ntp_attempt==normal);
 CHECK(gateway_application_ntp_test(&f.application,1)==0);
 f.now+=100U;CHECK(gateway_application_ntp_test(&f.application,0)==0&&f.application.time.next_ntp_attempt==normal);
 /* Start failures and timeout/reap each consume exactly three test attempts. */
 for(mode=0;mode<2U;++mode){
  prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
  f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
  f.ntp_poll_result=1;gateway_application_step(&f.application);
  CHECK(gateway_application_ntp_test(&f.application,1)==0);
  for(i=0;i<3U;++i){
   f.now=f.application.time.next_ntp_attempt;f.ntp_poll_result=0;f.ntp_start_result=mode?0:-1;
   gateway_application_step(&f.application);
   if(mode){f.now=f.application.ntp_deadline.at;gateway_application_step(&f.application);
    CHECK(f.application.ntp_terminating&&f.ntp_cancel_calls==i+1U);
    f.ntp_poll_result=-1;gateway_application_step(&f.application);CHECK(!f.application.ntp_terminating);}
   CHECK(f.application.time.ntp_test_attempts==i+1U);
   CHECK(f.application.time.next_ntp_attempt==f.now+(i<2U?60000U:GATEWAY_NTP_RETRY_MAX_MS));
  }
  CHECK(!f.application.time.ntp_test_active&&f.ntp_start_calls==4U);
  CHECK(f.application.coordinator.selected.settings.ntp_interval_hours==1U);
 }
 /* Shorten an overdue normal interval without unsigned underflow. */
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
 f.config.settings.ntp_enabled=1;f.config.settings.ntp_interval_hours=24U;strcpy(f.config.settings.ntp_server,"ntp.test");step_to_runtime(&f);
 f.ntp_poll_result=1;gateway_application_step(&f.application);f.now+=7200000U;
 f.application.coordinator.selected.settings.ntp_interval_hours=1U;f.ntp_poll_result=0;gateway_application_step(&f.application);
 CHECK(f.ntp_start_calls==2U&&f.ntp_cancel_calls==0U);
 f.application.coordinator.selected.settings.ntp_interval_hours=6U;gateway_application_step(&f.application);
 CHECK(f.ntp_start_calls==2U&&f.ntp_cancel_calls==0U);
 f.ntp_poll_result=1;gateway_application_step(&f.application);CHECK(f.application.time.next_ntp_attempt==f.now+21600000U);
 /* Diagnostic deadlines and return to a changed normal interval across wrap. */
 f.now=0xfffffff0U;CHECK(gateway_application_ntp_test(&f.application,1)==0);at=f.application.time.next_ntp_attempt;
 f.application.coordinator.selected.settings.ntp_interval_hours=24U;gateway_application_step(&f.application);
 CHECK(f.application.time.next_ntp_attempt==at);
 starts=f.ntp_start_calls;f.now=at-1U;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==starts);
 f.now=at;f.ntp_poll_result=0;gateway_application_step(&f.application);CHECK(f.ntp_start_calls==starts+1U);
 CHECK(gateway_application_ntp_test(&f.application,0)==0&&f.ntp_cancel_calls==0U);
 f.ntp_poll_result=1;gateway_application_step(&f.application);CHECK(f.application.time.next_ntp_attempt==f.now+86400000U);
 unchanged=f.application.coordinator.selected;
 CHECK(gateway_application_ntp_test(&f.application,1)==0);
 CHECK(memcmp(&unchanged,&f.application.coordinator.selected,sizeof(unchanged))==0&&f.stage_calls==0U);
 gateway_application_request_stop(&f.application);CHECK(gateway_application_ntp_test(&f.application,1)!=0);
 step_to_stop(&f);CHECK(!f.application.time.ntp_test_active);
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);step_to_runtime(&f);
 CHECK(!f.application.time.ntp_test_active&&f.application.time.ntp_test_attempts==0U);
}

static void ntp_observed_network(void)
{
    fixture_t f;unsigned int attempts;
    prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
    f.config.settings.ntp_enabled=1;strcpy(f.config.settings.ntp_server,"clock.example.test");
    f.application.network.environment=0;f.application.network.available=1;f.application.network.observed_valid=1;
    step_to_runtime(&f);
    CHECK(f.application.time.ntp_network_deferred&&f.ntp_start_calls==0);
    f.application.network.observed.lan[0].up=f.application.network.observed.lan[0].link=1;
    strcpy(f.application.network.observed.lan[0].address,"192.0.2.10");
    gateway_application_step(&f.application);CHECK(f.application.time.ntp_network_deferred&&f.ntp_start_calls==0);
    strcpy(f.application.network.observed.dns[0],"192.0.2.1");gateway_application_step(&f.application);
    CHECK(!f.application.time.ntp_network_deferred&&f.ntp_start_calls==1);
    f.ntp_poll_result=1;gateway_application_step(&f.application);CHECK(f.application.time.trust==GATEWAY_TIME_SYNCED);
    attempts=f.ntp_start_calls;f.now=f.application.time.next_ntp_attempt;
    f.application.network.observed.lan[0].link=0;gateway_application_step(&f.application);
    CHECK(f.application.time.ntp_network_deferred&&f.ntp_start_calls==attempts);
    CHECK(f.application.time.trust==GATEWAY_TIME_HOLDOVER);
    f.application.network.observed.lan[0].link=1;gateway_application_step(&f.application);
    CHECK(f.ntp_start_calls==attempts+1U&&!f.application.time.ntp_network_deferred);
    gateway_application_request_stop(&f.application);step_to_stop(&f);
}

static unsigned int light_calls,light_value;static int light_error;
static int light_fake(void*x,unsigned int on){(void)x;light_calls++;light_value=on;return light_error;}
static void backlight_contract(void){fixture_t f;unsigned int calls;gateway_port_config_t ports[8];
 prepare(&f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);light_calls=0;light_error=0;f.application.dependencies.backlight_set=light_fake;step_to_runtime(&f);
 CHECK(light_calls==1&&light_value==1&&f.application.backlight.command_known);
 memcpy(ports,f.application.coordinator.selected.ports,sizeof(ports));
 CHECK(!gateway_application_backlight_set(&f.application,0));CHECK(f.application.configuration_transaction.state==GATEWAY_CONFIG_TX_PROMOTING);
 gateway_application_step(&f.application);CHECK(!f.config.settings.backlight_on&&light_value==0&&light_calls==2);
 CHECK(!memcmp(ports,f.application.coordinator.selected.ports,sizeof(ports))&&f.provide_calls==1&&f.attach_calls==1&&f.application.shutdown_requests==0&&f.ntp_start_calls==0);
 {unsigned int i;for(i=0;i<8;i++)CHECK(f.backend[i].reconfigures==0&&f.backend[i].stops==0);}
 calls=light_calls;f.stage_result=GATEWAY_CONFIG_IO_ERROR;CHECK(gateway_application_backlight_set(&f.application,1)<0);gateway_application_step(&f.application);CHECK(light_calls==calls&&!f.config.settings.backlight_on);
 f.stage_result=GATEWAY_CONFIG_OK;f.promote_result=GATEWAY_CONFIG_IO_ERROR;CHECK(!gateway_application_backlight_set(&f.application,1));gateway_application_step(&f.application);gateway_application_step(&f.application);CHECK(f.application.configuration_transaction.state==GATEWAY_CONFIG_TX_FAILED&&light_calls==calls&&!f.config.settings.backlight_on);{unsigned int i;for(i=0;i<8;i++)CHECK(f.backend[i].reconfigures==0&&f.backend[i].stops==0);}f.promote_result=GATEWAY_CONFIG_OK;
 light_error=5;CHECK(!gateway_application_backlight_set(&f.application,1));gateway_application_step(&f.application);CHECK(f.config.settings.backlight_on&&!f.application.backlight.command_known&&f.application.backlight.last_error==5);
 calls=light_calls;gateway_application_step(&f.application);CHECK(light_calls==calls);light_error=0;CHECK(!gateway_application_backlight_set(&f.application,1));gateway_application_step(&f.application);CHECK(f.application.backlight.command_known&&light_calls==calls+1);
 CHECK(!gateway_application_backlight_set(&f.application,0));gateway_application_step(&f.application);gateway_application_request_stop(&f.application);step_to_stop(&f);CHECK(!gateway_application_init(&f.application,&f.dependencies));f.application.dependencies.backlight_set=light_fake;step_to_runtime(&f);CHECK(light_value==0&&f.application.backlight.command_known);
 gateway_application_request_stop(&f.application);step_to_stop(&f);
}

int main(void){backlight_contract();ntp_observed_network();ntp_test_boundaries();ntp_interval_and_bounded_test();rtc_and_periodic_ntp();ready_and_shutdown();degraded();safe_mode();fatal_and_early_stop();cleanup_failure();metadata_and_bounds();production_binding_assembly();configuration_transactions();runtime_uart_identity_is_fixed();activation_failure_rolls_back();shutdown_cancels_unpromoted_candidate();system_time_api_and_monotonic_separation();ntp_state_retry_timeout_and_manual_transition();persistent_ntp_policy_transaction();printf("application checks=%u failed=%u bytes=%u adapters=%u max_fds=%u coordinator=%u\n",checks,failed,gateway_application_memory_bytes(),gateway_application_adapter_bytes(),gateway_application_max_fds(),gateway_coordinator_memory_bytes());return failed?1:0;}
