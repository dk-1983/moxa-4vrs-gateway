#include <stdio.h>
#include <string.h>
#include "console/gateway_console.h"

static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %u %s\n",(unsigned)__LINE__,#x);}}while(0)
unsigned char diagnostic_snapshot_layout[sizeof(gateway_diagnostic_snapshot_t)];
unsigned char gateway_console_layout[sizeof(gateway_console_t)];
static core_tick_t clock_now(void*x){(void)x;return 1234;}
static int writes(void*x,const char*s,size_t n){char*b=(char*)x;size_t at=strlen(b),room=16383U-at;if(n>room)n=room;memcpy(b+at,s,n);b[at+n]='\0';return 0;}
static int next_key_value=-1;static int keys(void*x){int k=next_key_value;(void)x;next_key_value=-1;return k;}static int flush(void*x){(void)x;return 0;}static const gateway_console_ops_t cops={writes,keys,flush};
static void fixture(gateway_application_t*a){gateway_port_config_t p[GATEWAY_PORT_COUNT];gateway_port_binding_t b[GATEWAY_PORT_COUNT];unsigned i;memset(a,0,sizeof(*a));memset(b,0,sizeof(b));gateway_configuration_defaults(p);for(i=0;i<GATEWAY_PORT_COUNT;++i)p[i].enabled=0;p[0].enabled=1;strcpy(p[0].bind_address,"10.0.2.15");p[0].endpoint_port=1502;gateway_controller_init(&a->coordinator.controller,p,b);a->coordinator.controller_initialized=1;a->coordinator.controller_driver=gateway_default_controller_driver();a->coordinator.state=GATEWAY_APP_READY;a->coordinator.config_source=GATEWAY_CONFIG_SOURCE_ACTIVE;a->coordinator.persistence_result=GATEWAY_CONFIG_OK;a->coordinator.progress_numerator=GATEWAY_STARTUP_PROGRESS_TOTAL;a->coordinator.started_at=100;a->coordinator.selected.schema_version=GATEWAY_CONFIG_SCHEMA_VERSION;a->coordinator.controller.ports[0].lifecycle=GATEWAY_PORT_READY;a->coordinator.controller.ports[0].runtime.stats.accepted=7;a->coordinator.controller.ports[0].runtime.stats.completed=6;a->coordinator.controller.ports[0].runtime.stats.timeouts=1;a->coordinator.controller.ports[0].runtime.stats.recoveries=1;a->coordinator.controller.ports[0].runtime.stats.stale_responses=2;a->coordinator.controller.ports[0].runtime.stats.queue_high_water=3;a->dependencies.clock=clock_now;a->dependencies.configuration_directory="/test/config";strcpy(a->platform.model,"Moxa UC-7420 Plus");strcpy(a->platform.kernel,"Linux test");strcpy(a->platform.architecture,"ARM XScale");a->platform.word_bits=32;a->platform.big_endian=1;a->process_state=GATEWAY_PROCESS_RUNNING;a->time.trust=GATEWAY_TIME_SYNCED;a->time.last_ntp_result=GATEWAY_NTP_SUCCEEDED;strcpy(a->time.ntp_server,"ntp.test");a->time.ntp_attempts=1;a->time.last_sync=1200;}
static void raw_support(void)
{
    gateway_diagnostic_snapshot_t s;char out[GATEWAY_DIAGNOSTIC_SUPPORT_MAX];size_t n;unsigned i;
    memset(&s,0,sizeof(s));
    s.application.time.trust=GATEWAY_TIME_HOLDOVER;
    s.application.time.rtc=GATEWAY_RTC_WRITE_FAILED;
    s.application.time.last_ntp_result=GATEWAY_NTP_NOT_CONFIGURED;
    s.application.time.ntp_attempts=s.application.time.last_ntp_attempt=0xffffffffU;
    s.application.time.last_sync=s.application.time.last_rtc_save=0xffffffffU;
    s.application.time.rtc_save_failures=s.application.time.next_ntp_attempt=0xffffffffU;
    s.application.time.ntp_failures=0xffffffffU;
    memset(s.application.time.ntp_server,'x',sizeof(s.application.time.ntp_server)-1U);
    for(i=0;i<GATEWAY_PORT_COUNT;++i){
        gateway_port_health_t*p=&s.application.coordinator.gateway.port[i];
        p->enabled=1;p->lifecycle=GATEWAY_PORT_RECONFIGURING;
        strcpy(p->config.bind_address,"255.255.255.255");p->config.endpoint_port=65535;
        p->connected_clients=0xffffffffU;
        p->transactions.completed=p->transactions.timeouts=p->transactions.recoveries=p->transactions.stale_responses=0xffffffffU;
        p->transport.crc_failures=p->transport.framing_failures=p->transport.unit_mismatches=p->transport.function_mismatches=p->transport.leading_garbage=0xffffffffU;
        p->config.transport=TRANSPORT_RAW_UDP;
        p->transport.raw_network_received=p->transport.raw_network_sent=0xffffffffU;
        p->transport.raw_serial_read=p->transport.raw_serial_written=0xffffffffU;
        p->transport.raw_rejected_peers=p->transport.raw_discarded_serial=0xffffffffU;
        p->transport.tx_overflow=p->transport.write_deadline_expired=p->transport.gateway_path_unavailable=0xffffffffU;
    }
    CHECK(gateway_diagnostics_render_support(&s,out,sizeof(out),&n)==GATEWAY_RENDER_OK);
    CHECK(strstr(out,"port.8.raw_serial_tx=4294967295")!=0);
    CHECK(strstr(out,"port.1.raw_io_errors=4294967295")!=0);
    CHECK(strstr(out,"rtc_state=Write failed")!=0&&strstr(out,"time_trust=HOLDOVER")!=0);
    CHECK(strstr(out,"next_ntp_attempt_ms=4294967295")!=0);
    CHECK(n<sizeof(out));
    printf("raw support bytes=%u capacity=%u\n",(unsigned)n,(unsigned)sizeof(out));
}
int main(void){gateway_application_t a;gateway_diagnostic_snapshot_t s,s2;gateway_console_t c;char out[16384],tiny[8];size_t n;raw_support();fixture(&a);CHECK(gateway_diagnostics_capture(&a,&s)==0);CHECK(s.application.coordinator.progress_percent==100);CHECK(s.application.coordinator.gateway.port[0].transactions.timeouts==1);s.application.coordinator.gateway.port[0].transport.crc_failures=2;s.application.coordinator.gateway.port[0].transport.framing_failures=3;s.application.coordinator.gateway.port[0].transport.unit_mismatches=4;s.application.coordinator.gateway.port[0].transport.function_mismatches=5;s.application.coordinator.gateway.port[0].transport.leading_garbage=1;CHECK(gateway_diagnostics_render_summary(&s,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"P8")!=0);CHECK(strstr(out,"READY")!=0);CHECK(gateway_diagnostics_render_port(&s,0,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"timeouts=1")!=0);CHECK(strstr(out,"stale=2")!=0);CHECK(strstr(out,"crc_failures=2")!=0);CHECK(strstr(out,"framing_failures=3")!=0);CHECK(strstr(out,"unit_mismatch=4")!=0);CHECK(strstr(out,"function_mismatch=5")!=0);CHECK(strstr(out,"leading_garbage=1")!=0);CHECK(gateway_diagnostics_render_configuration(&s,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"source=ACTIVE")!=0);CHECK(gateway_diagnostics_render_system(&s,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"ARM XScale")!=0);CHECK(strstr(out,"time_trust=SYNCED")!=0&&strstr(out,"ntp_server=ntp.test")!=0);s.startup_event_count=1;s.startup_events[0].sequence=9;s.startup_events[0].stage=GATEWAY_APP_READY;s.startup_events[0].progress_numerator=14;s.startup_events[0].progress_denominator=14;s.startup_events[0].progress_percent=100;s.startup_events[0].port_index=GATEWAY_NO_PORT;CHECK(gateway_diagnostics_render_startup(&s,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"seq=9")!=0);CHECK(gateway_diagnostics_render_support(&s,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"port.8.state=DISABLED")!=0);CHECK(strstr(out,"time_trust=SYNCED")!=0&&strstr(out,"ntp_result=SUCCEEDED")!=0);CHECK(strstr(out,"port.1.leading_garbage=1")!=0);CHECK(gateway_diagnostics_render_summary(&s,tiny,sizeof(tiny),&n)==GATEWAY_RENDER_TRUNCATED);CHECK(n<sizeof(tiny));CHECK(gateway_diagnostics_capture(&a,&s2)==0);CHECK(s2.application.coordinator.gateway.port[0].transactions.completed==s.application.coordinator.gateway.port[0].transactions.completed);a.coordinator.state=GATEWAY_APP_DEGRADED;CHECK(gateway_diagnostics_capture(&a,&s2)==0);CHECK(gateway_diagnostics_render_summary(&s2,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"State: DEGRADED")!=0);a.coordinator.state=GATEWAY_APP_SAFE_MODE;a.coordinator.error=GATEWAY_APP_ERROR_CONFIG_INVALID;a.coordinator.controller_initialized=0;CHECK(gateway_diagnostics_capture(&a,&s2)==0);CHECK(gateway_diagnostics_render_summary(&s2,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"SAFE_MODE")!=0);CHECK(gateway_diagnostics_render_configuration(&s2,out,sizeof(out),&n)==GATEWAY_RENDER_OK);CHECK(strstr(out,"safe_mode_reason=1")!=0);fixture(&a);out[0]='\0';CHECK(gateway_console_init(&c,&cops,out)==0);CHECK(gateway_console_step(&c,&a)==0);CHECK(strstr(out,"Gateway status")!=0);next_key_value='1';CHECK(gateway_console_step(&c,&a)==0);CHECK(gateway_console_step(&c,&a)==0);CHECK(strstr(out,"State: READY")!=0);next_key_value='b';gateway_console_step(&c,&a);gateway_console_step(&c,&a);next_key_value='5';CHECK(gateway_console_step(&c,&a)==0);CHECK(c.exit_requested==1);CHECK(a.run_control.stop_requested==1);printf("gateway_diagnostics checks=%u failed=%u snapshot=%u console=%u\n",checks,failures,gateway_diagnostic_snapshot_bytes(),gateway_console_memory_bytes());return failures?1:0;}
