#include <stdio.h>
#include "version.h"
#include <string.h>

#include "panel/gateway_panel.h"
#include "panel/gateway_panel_moxa.h"

const unsigned char panel_layout[sizeof(gateway_panel_t)]={0};
const unsigned char panel_screen_layout[sizeof(gateway_panel_screen_t)]={0};
const unsigned char panel_moxa_layout[sizeof(gateway_panel_moxa_t)]={0};

static unsigned int checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)

typedef struct fake_panel {
    int display_open_result,keypad_open_result,draw_result,poll_error;
    unsigned int display_opens,keypad_opens,draws,display_closes,keypad_closes;
    unsigned int keys[64],key_count,key_index;
    gateway_panel_screen_t last;
    unsigned int held;
    unsigned int sentinel;
} fake_panel_t;

static int dopen(void*x){fake_panel_t*f=x;++f->display_opens;return f->display_open_result;}
static int kopen(void*x){fake_panel_t*f=x;++f->keypad_opens;return f->keypad_open_result;}
static int draw(void*x,const gateway_panel_screen_t*s){fake_panel_t*f=x;++f->draws;f->last=*s;return f->draw_result;}
static int poll(void*x,unsigned int*k){fake_panel_t*f=x;if(f->poll_error)return f->poll_error;if(f->key_index>=f->key_count)return 0;*k=f->keys[f->key_index++];return 1;}
static void dclose(void*x){++((fake_panel_t*)x)->display_closes;}
static void kclose(void*x){++((fake_panel_t*)x)->keypad_closes;}
static const gateway_panel_ops_t ops={dopen,kopen,draw,poll,dclose,kclose};
static core_tick_t test_now=1000;
static core_tick_t now(void*x){(void)x;return test_now;}
static gateway_config_result_t stage(void*x,const char*d,const gateway_persistent_config_t*c){(void)x;(void)d;(void)c;return GATEWAY_CONFIG_OK;}
static gateway_config_result_t promote(void*x,const char*d){(void)x;(void)d;return GATEWAY_CONFIG_OK;}
static void discard(void*x,const char*d){(void)x;(void)d;}
static gateway_system_time_t fake_time={2026U,9U,5U,23U,32U,15U};
static gateway_system_time_result_t fake_time_set_result=GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED;
static unsigned int fake_time_sets;
static gateway_system_time_result_t get_time(void*x,gateway_system_time_t*v){(void)x;*v=fake_time;return GATEWAY_SYSTEM_TIME_OK;}
static gateway_system_time_result_t set_time(void*x,const gateway_system_time_t*v){(void)x;++fake_time_sets;fake_time=*v;return fake_time_set_result;}
static void app_fixture(gateway_application_t*a,gateway_application_state_t state)
{gateway_port_binding_t b[8];unsigned int i;memset(a,0,sizeof(*a));memset(b,0,sizeof(b));fake_time=(gateway_system_time_t){2026U,9U,5U,23U,32U,15U};fake_time_set_result=GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED;fake_time_sets=0;gateway_persistent_safe_mode(&a->coordinator.selected);gateway_controller_init(&a->coordinator.controller,a->coordinator.selected.ports,b);a->coordinator.controller_initialized=1;a->coordinator.controller_driver=gateway_default_controller_driver();a->coordinator.state=state;a->coordinator.config_source=state==GATEWAY_APP_SAFE_MODE?GATEWAY_CONFIG_SOURCE_SAFE_MODE:GATEWAY_CONFIG_SOURCE_ACTIVE;a->coordinator.progress_numerator=state==GATEWAY_APP_BOOTSTRAP?0:GATEWAY_STARTUP_PROGRESS_TOTAL;a->coordinator.started_at=0;a->process_state=GATEWAY_PROCESS_RUNNING;a->time.ntp_interval_hours=1U;a->time.trust=GATEWAY_TIME_UNSYNCED;a->dependencies.clock=now;a->dependencies.system_time_get=get_time;a->dependencies.system_time_set=set_time;a->dependencies.configuration_directory="test";a->dependencies.stage_configuration=stage;a->dependencies.promote_configuration=promote;a->dependencies.discard_configuration=discard;strcpy(a->platform.model,"UC-7420 Plus");strcpy(a->platform.kernel,"2.6.10");strcpy(a->platform.architecture,"ARM XScale");a->platform.word_bits=32;a->platform.big_endian=1;for(i=0;i<8U;++i)a->coordinator.controller.ports[i].lifecycle=GATEWAY_PORT_DISABLED;}
static int has(const gateway_panel_screen_t*s,const char*t){unsigned int r;for(r=0;r<8U;++r)if(strstr(s->row[r],t))return 1;return 0;}
static int rows_bounded(const gateway_panel_screen_t*s){unsigned int r;for(r=0;r<8U;++r)if(s->row[r][16]!='\0'||strlen(s->row[r])>16U)return 0;return 1;}
static void push(fake_panel_t*f,unsigned int k){f->keys[f->key_count++]=k;}
static void step(gateway_panel_t*p,gateway_application_t*a,fake_panel_t*f,unsigned int k){push(f,k);gateway_panel_step(p,a);}

static void unavailable_and_failure_isolation(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);f.display_open_result=5;f.keypad_open_result=6;CHECK(gateway_panel_init(&p,&ops,&f)==0);CHECK(!p.health.display_available&&!p.health.keypad_available);gateway_panel_step(&p,&a);CHECK(a.process_state==GATEWAY_PROCESS_RUNNING&&f.draws==0);gateway_panel_shutdown(&p);memset(&f,0,sizeof(f));f.draw_result=9;CHECK(gateway_panel_init(&p,&ops,&f)==0);gateway_panel_step(&p,&a);CHECK(!p.health.display_available&&p.health.display_errors==1&&f.display_closes==1);f.poll_error=-7;gateway_panel_step(&p,&a);CHECK(!p.health.keypad_available&&p.health.keypad_errors==1&&f.keypad_closes==1);CHECK(a.run_control.stop_requested==0);gateway_panel_shutdown(&p);}
static void startup_home_and_dirty(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;unsigned int draws;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_BOOTSTRAP);CHECK(gateway_panel_init(&p,&ops,&f)==0);gateway_panel_step(&p,&a);CHECK(has(&f.last,"4VRS Gateway")&&has(&f.last,FOURVRS_VERSION)&&has(&f.last,"0%"));draws=f.draws;gateway_panel_step(&p,&a);CHECK(f.draws==draws&&p.health.suppressed_render_count>0);a.coordinator.state=GATEWAY_APP_READY;a.coordinator.progress_numerator=14;gateway_panel_step(&p,&a);CHECK(p.view==GATEWAY_PANEL_HOME&&has(&f.last,"READY")&&has(&f.last,"F1 Help F3 Menu")&&has(&f.last,"TIME UNSYNC"));gateway_panel_shutdown(&p);CHECK(f.display_closes==1&&f.keypad_closes==1);}

static void help_and_main_menu_layout(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;unsigned int i;const char*items[]={"Status","Ports","Configuration","Diagnostics","System","Shutdown","About"};memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_HELP&&has(&f.last,"Button Help: 1/2")&&has(&f.last,"F1 Back/Cancel")&&has(&f.last,"F2 Previous")&&has(&f.last,"or Decrease")&&has(&f.last,"F3 Select / OK")&&has(&f.last,"F4 Next page"));CHECK(rows_bounded(&f.last));step(&p,&a,&f,3);CHECK(p.help_page==1&&has(&f.last,"Button Help: 2/2")&&has(&f.last,"F4 Next")&&has(&f.last,"or Increase")&&has(&f.last,"F5 Edit/Context")&&has(&f.last,"F2 Prev page")&&has(&f.last,"F1 Back"));step(&p,&a,&f,1);CHECK(p.help_page==0);step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_HOME);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_MENU&&has(&f.last,"Main Menu:")&&!has(&f.last,"F1<")&&!has(&f.last,"F2^")&&!has(&f.last,"F4v"));for(i=0;i<7U;++i)CHECK(has(&f.last,items[i]));CHECK(rows_bounded(&f.last));gateway_panel_shutdown(&p);}
static void navigation_and_views(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_DEGRADED);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);CHECK(has(&f.last,"DEGRADED"));step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_MENU);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_STATUS&&has(&f.last,"Status:"));step(&p,&a,&f,0);step(&p,&a,&f,3);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_PORTS&&has(&f.last,"P8")==0);for(;p.selected<7U;)step(&p,&a,&f,3);CHECK(has(&f.last,"P8"));step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_PORT_DETAIL&&has(&f.last,"ttyM7"));step(&p,&a,&f,3);CHECK(has(&f.last,"Accepted"));step(&p,&a,&f,3);CHECK(has(&f.last,"Garbage"));gateway_panel_shutdown(&p);}
static void safe_mode_system_diagnostics_events(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_SAFE_MODE);a.coordinator.error=GATEWAY_APP_ERROR_CONFIG_INVALID;a.coordinator.event_count=1;a.coordinator.events[0].sequence=7;a.coordinator.events[0].stage=GATEWAY_APP_SAFE_MODE;a.coordinator.events[0].progress_percent=100;a.coordinator.events[0].progress_denominator=14;gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);CHECK(has(&f.last,"SAFE MODE")&&a.coordinator.controller.ports[0].runtime.state==PORT_DISABLED);p.view=GATEWAY_PANEL_DIAGNOSTICS;gateway_panel_step(&p,&a);CHECK(has(&f.last,"Diagnostics"));step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_STARTUP_EVENTS&&has(&f.last,"Sequence 7"));p.view=GATEWAY_PANEL_SYSTEM;p.system_page=1;gateway_panel_step(&p,&a);CHECK(has(&f.last,"Platform:")&&has(&f.last,"ARM XScale")&&has(&f.last,"F4 Disp F5 NTP"));p.view=GATEWAY_PANEL_ABOUT;gateway_panel_step(&p,&a);CHECK(has(&f.last,FOURVRS_VERSION));gateway_panel_shutdown(&p);}

static void system_time_view_edit_and_failures(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;unsigned int draws,i;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);step(&p,&a,&f,2);p.selected=4U;p.system_page=1U;step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_SYSTEM&&p.system_page==0U&&has(&f.last,"Date & Time:")&&has(&f.last,"2026-09-05")&&has(&f.last,"23:32:15")&&has(&f.last,"Time: Unsynced")&&has(&f.last,"RTC: Unknown")&&has(&f.last,"F5 Set Time")&&has(&f.last,"F4 Platform"));draws=f.draws;gateway_panel_step(&p,&a);CHECK(f.draws==draws);a.time.trust=GATEWAY_TIME_SYNCED;gateway_panel_step(&p,&a);CHECK(has(&f.last,"Time: Synced")&&f.draws==draws+1U);a.time.rtc=GATEWAY_RTC_SAVED;gateway_panel_step(&p,&a);CHECK(has(&f.last,"RTC: Saved"));a.time.rtc=GATEWAY_RTC_WRITE_FAILED;a.time.trust=GATEWAY_TIME_HOLDOVER;gateway_panel_step(&p,&a);CHECK(has(&f.last,"RTC: Write fail")&&has(&f.last,"Time: Holdover"));a.time.trust=GATEWAY_TIME_UNSYNCED;step(&p,&a,&f,3);CHECK(p.system_page==1&&has(&f.last,"Platform:")&&has(&f.last,"ARM XScale")&&has(&f.last,"F4 Disp F5 NTP")&&has(&f.last,"F2 Date F1 Back"));step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_NTP_SETTINGS&&has(&f.last,"Network Time:")&&has(&f.last,"Every: 1 h"));step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_SYSTEM&&p.system_page==1U&&has(&f.last,"Platform:"));step(&p,&a,&f,1);CHECK(p.system_page==0U&&has(&f.last,"Date & Time:"));step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_MENU);p.selected=4U;p.system_page=1U;step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_SYSTEM&&p.system_page==0U&&has(&f.last,"Date & Time:"));step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_TIME_EDIT&&p.time_candidate.year==2026U);step(&p,&a,&f,3);CHECK(p.time_candidate.year==2027U);step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_SYSTEM&&fake_time_sets==0U);step(&p,&a,&f,4);for(i=0;i<6U;++i)step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_TIME_CONFIRM&&fake_time_sets==0U&&has(&f.last,"Set system time?"));step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_TIME_EDIT&&fake_time_sets==0U);step(&p,&a,&f,2);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_TIME_RESULT&&fake_time_sets==1U&&has(&f.last,"SYSTEM TIME SET")&&has(&f.last,"RTC NOT SYNCED")&&a.time.trust==GATEWAY_TIME_MANUAL);p.view=GATEWAY_PANEL_TIME_EDIT;p.time_candidate=(gateway_system_time_t){2025U,2U,29U,12U,0U,0U};p.time_field=5;step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_TIME_RESULT&&p.time_result==GATEWAY_SYSTEM_TIME_INVALID&&fake_time_sets==1U);p.view=GATEWAY_PANEL_TIME_CONFIRM;gateway_application_revision(&a,p.draft_token);p.time_candidate=(gateway_system_time_t){2024U,2U,29U,12U,0U,0U};fake_time_set_result=GATEWAY_SYSTEM_TIME_SET_FAILED;step(&p,&a,&f,2);CHECK(p.time_result==GATEWAY_SYSTEM_TIME_SET_FAILED&&has(&f.last,"SET FAILED")&&a.process_state==GATEWAY_PROCESS_RUNNING);p.view=GATEWAY_PANEL_TIME_CONFIRM;gateway_application_revision(&a,p.draft_token);fake_time_set_result=GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED;step(&p,&a,&f,2);CHECK(p.time_result==GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED&&has(&f.last,"RTC SYNC FAILED")&&a.process_state==GATEWAY_PROCESS_RUNNING);CHECK(rows_bounded(&f.last));gateway_panel_shutdown(&p);}
static void editing_confirmation_and_results(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;gateway_port_config_t original;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_CONFIG_PORTS;p.selected=0;p.candidate=a.coordinator.selected;gateway_application_revision(&a,p.draft_token);p.candidate_loaded=1;gateway_panel_step(&p,&a);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_CONFIG_FIELDS);original=p.candidate.ports[0];step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_EDIT_FIELD);step(&p,&a,&f,3);CHECK(p.candidate.ports[0].enabled!=original.enabled);step(&p,&a,&f,0);CHECK(p.candidate.ports[0].enabled==original.enabled&&p.view==GATEWAY_PANEL_CONFIG_FIELDS);step(&p,&a,&f,4);step(&p,&a,&f,3);step(&p,&a,&f,2);CHECK(p.candidate.ports[0].enabled!=original.enabled&&p.candidate.ports[0].revision==original.revision+1U);p.field_index=10;p.view=GATEWAY_PANEL_CONFIG_FIELDS;gateway_panel_step(&p,&a);step(&p,&a,&f,4);step(&p,&a,&f,3);step(&p,&a,&f,2);CHECK(p.candidate.ports[0].endpoint_port==original.endpoint_port+1U);p.field_index=11;p.view=GATEWAY_PANEL_CONFIG_FIELDS;gateway_panel_step(&p,&a);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_APPLY_CONFIRM&&p.network_change);step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_CONFIG_FIELDS);p.candidate=a.coordinator.selected;gateway_application_revision(&a,p.draft_token);p.field_index=11;step(&p,&a,&f,2);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_APPLY_RESULT&&gateway_application_configuration_state(&a)==GATEWAY_CONFIG_TX_UNCHANGED);gateway_panel_step(&p,&a);CHECK(has(&f.last,"NO CHANGES"));a.configuration_transaction.state=GATEWAY_CONFIG_TX_FAILED;a.configuration_transaction.rollback_failed=0;gateway_panel_step(&p,&a);CHECK(has(&f.last,"ROLLED BACK"));a.configuration_transaction.state=GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN;gateway_panel_step(&p,&a);CHECK(has(&f.last,"DURABILITY?"));gateway_panel_shutdown(&p);}
static void shutdown_confirmation_and_bounds(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;unsigned int r;memset(&f,0,sizeof(f));f.sentinel=0xa55aa55aU;app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_SHUTDOWN_CONFIRM;gateway_application_revision(&a,p.draft_token);gateway_panel_step(&p,&a);CHECK(has(&f.last,"Confirm:")&&has(&f.last,"Stop gateway?")&&has(&f.last,"Reboot unavailab"));step(&p,&a,&f,0);CHECK(!a.run_control.stop_requested);p.view=GATEWAY_PANEL_SHUTDOWN_CONFIRM;gateway_application_revision(&a,p.draft_token);step(&p,&a,&f,2);CHECK(a.run_control.stop_requested==1);for(r=0;r<8U;++r)CHECK(f.last.row[r][16]=='\0');CHECK(f.sentinel==0xa55aa55aU);CHECK(gateway_panel_screen_bytes()==sizeof(gateway_panel_screen_t));CHECK(gateway_panel_memory_bytes()==sizeof(gateway_panel_t));gateway_panel_shutdown(&p);}

static void title_dictionary_and_bounds(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;unsigned int i;const gateway_panel_view_t views[]={GATEWAY_PANEL_STATUS,GATEWAY_PANEL_PORTS,GATEWAY_PANEL_PORT_DETAIL,GATEWAY_PANEL_CONFIG_PORTS,GATEWAY_PANEL_CONFIG_FIELDS,GATEWAY_PANEL_EDIT_FIELD,GATEWAY_PANEL_APPLY_CONFIRM,GATEWAY_PANEL_APPLY_RESULT,GATEWAY_PANEL_DIAGNOSTICS,GATEWAY_PANEL_STARTUP_EVENTS,GATEWAY_PANEL_SYSTEM,GATEWAY_PANEL_SHUTDOWN_CONFIRM,GATEWAY_PANEL_ABOUT};const char*titles[]={"Status:","Ports:","Port 1:","Config:","Config P1:","Edit Field:","Confirm:","Config Result:","Diagnostics:","Startup Events:","Date & Time:","Confirm:","About:"};memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);p.candidate=a.coordinator.selected;gateway_application_revision(&a,p.draft_token);p.candidate_loaded=1;for(i=0;i<sizeof(views)/sizeof(views[0]);++i){p.view=views[i];gateway_panel_step(&p,&a);CHECK(has(&f.last,titles[i]));CHECK(rows_bounded(&f.last));CHECK(strchr(f.last.row[0],':')!=0);}gateway_panel_shutdown(&p);}

static void fixed_uart_and_local_validation(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;gateway_configuration_transaction_state_t state;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_CONFIG_FIELDS;p.field_index=1;p.port_index=0;p.candidate=a.coordinator.selected;gateway_application_revision(&a,p.draft_token);p.candidate_loaded=1;gateway_panel_step(&p,&a);CHECK(has(&f.last,"Fixed at runtime"));step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_CONFIG_FIELDS);p.candidate.ports[0].enabled=1;p.candidate.ports[1].enabled=1;p.candidate.ports[0].endpoint_port=502;p.candidate.ports[1].endpoint_port=502;p.field_index=11;state=a.configuration_transaction.state;step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_APPLY_RESULT&&p.local_validation_failed==GATEWAY_ERROR_DUPLICATE_ENDPOINT);CHECK(a.configuration_transaction.state==state);CHECK(has(&f.last,"NOT APPLIED"));gateway_panel_shutdown(&p);}

static void startup_port_result_and_fatal(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_BOOTSTRAP);a.coordinator.progress_numerator=8;a.coordinator.event_count=1;a.coordinator.events[0].port_index=2;a.coordinator.events[0].result=GATEWAY_EVENT_SKIPPED;a.coordinator.events[0].stage=GATEWAY_APP_STARTING_PORTS;a.coordinator.events[0].progress_percent=57;gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);CHECK(has(&f.last,"P3 OFF")&&has(&f.last,"57%"));a.fatal_seen=1;a.coordinator.error=GATEWAY_APP_ERROR_CONTROLLER_INIT;a.coordinator.state=GATEWAY_APP_SHUTTING_DOWN;gateway_panel_step(&p,&a);CHECK(p.view==GATEWAY_PANEL_HOME&&has(&f.last,"FATAL ERROR"));gateway_panel_shutdown(&p);}

static void ntp_settings_edit_cancel_and_apply(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);a.coordinator.selected.settings.ntp_enabled=1;strcpy(a.coordinator.selected.settings.ntp_server,"10.0.0.10");a.time.ntp_interval_hours=1U;a.time.ntp_enabled=1;strcpy(a.time.ntp_server,"10.0.0.10");a.time.last_ntp_result=GATEWAY_NTP_FAILED;gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_SYSTEM;p.system_page=1;gateway_panel_step(&p,&a);CHECK(has(&f.last,"Platform:")&&has(&f.last,"F4 Disp F5 NTP"));step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_NTP_SETTINGS&&has(&f.last,"Network Time:")&&has(&f.last,"Every: 1 h")&&has(&f.last,"10.0.0.10")&&has(&f.last,"State: Failed"));step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_NTP_EDIT);step(&p,&a,&f,2);p.ntp_cursor=7;step(&p,&a,&f,3);CHECK(strcmp(p.candidate.settings.ntp_server,"10.0.0.20")==0);step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_NTP_SETTINGS&&strcmp(a.coordinator.selected.settings.ntp_server,"10.0.0.10")==0);step(&p,&a,&f,4);step(&p,&a,&f,2);p.ntp_cursor=7;step(&p,&a,&f,3);step(&p,&a,&f,4);CHECK(p.ntp_field==2U&&has(&f.last,"Every: 1 h"));step(&p,&a,&f,3);CHECK(p.candidate.settings.ntp_interval_hours==6U&&has(&f.last,"Every: 6 h"));step(&p,&a,&f,3);CHECK(p.candidate.settings.ntp_interval_hours==24U);step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_NTP_CONFIRM&&has(&f.last,"10.0.0.20")&&has(&f.last,"Every: 24 h"));step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_NTP_RESULT&&a.configuration_transaction.state==GATEWAY_CONFIG_TX_PROMOTING);CHECK(a.process_state==GATEWAY_PROCESS_RUNNING&&a.coordinator.controller.ports[0].lifecycle==GATEWAY_PORT_DISABLED);CHECK(rows_bounded(&f.last));gateway_panel_shutdown(&p);}

static core_tick_t wrap_now(void *context){return *(core_tick_t *)context;}
static void time_refresh_wrap(void)
{
 gateway_panel_t p;gateway_application_t a;fake_panel_t f;core_tick_t tick=0xfffffff0U;unsigned draws;
 memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);
 a.dependencies.clock=wrap_now;a.dependencies.clock_context=&tick;a.coordinator.started_at=0;
 gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_SYSTEM;
 gateway_panel_step(&p,&a);draws=f.draws;fake_time.second=16U;
 tick=0xfffffff1U;gateway_panel_step(&p,&a);CHECK(f.draws==draws&&has(&f.last,"23:32:15"));
 tick=983U;gateway_panel_step(&p,&a);CHECK(has(&f.last,"23:32:15"));
 tick=984U;gateway_panel_step(&p,&a);CHECK(has(&f.last,"23:32:16"));
 gateway_panel_shutdown(&p);
}

static void ntp_test_keys_and_cancel(void)
{
 gateway_panel_t p;gateway_application_t a;fake_panel_t f;gateway_persistent_config_t original;
 memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);
 a.time.ntp_enabled=1;strcpy(a.time.ntp_server,"ntp.test");
 a.coordinator.selected.settings.ntp_enabled=1;strcpy(a.coordinator.selected.settings.ntp_server,"ntp.test");
 original=a.coordinator.selected;gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);
 p.view=GATEWAY_PANEL_NTP_SETTINGS;gateway_panel_step(&p,&a);
 CHECK(has(&f.last,"F4 Test F5 Edit")&&rows_bounded(&f.last));
 step(&p,&a,&f,3);CHECK(a.time.ntp_test_active&&has(&f.last,"Test 1m: 0/3")&&has(&f.last,"F4 Stop F5 Edit"));
 step(&p,&a,&f,3);CHECK(!a.time.ntp_test_active&&has(&f.last,"F4 Test F5 Edit"));
 CHECK(memcmp(&original,&a.coordinator.selected,sizeof(original))==0&&a.configuration_transaction.state==GATEWAY_CONFIG_TX_IDLE);
 step(&p,&a,&f,4);step(&p,&a,&f,4);CHECK(p.ntp_field==2U);
 step(&p,&a,&f,1);CHECK(p.candidate.settings.ntp_interval_hours==24U);
 step(&p,&a,&f,3);CHECK(p.candidate.settings.ntp_interval_hours==1U);
 step(&p,&a,&f,3);step(&p,&a,&f,4);CHECK(p.view==GATEWAY_PANEL_NTP_CONFIRM&&has(&f.last,"Every: 6 h"));
 step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_NTP_EDIT);
 step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_NTP_SETTINGS&&p.candidate.settings.ntp_interval_hours==1U);
 CHECK(memcmp(&original,&a.coordinator.selected,sizeof(original))==0);
 step(&p,&a,&f,4);p.candidate.settings.ntp_server[0]='\0';step(&p,&a,&f,4);step(&p,&a,&f,4);
 CHECK(p.view==GATEWAY_PANEL_NTP_RESULT&&has(&f.last,"INVALID SETTINGS")&&rows_bounded(&f.last));
 CHECK(a.configuration_transaction.state==GATEWAY_CONFIG_TX_IDLE);
 gateway_panel_shutdown(&p);
}

static void backlight_menu(void){gateway_panel_t p;gateway_application_t a;fake_panel_t f;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);CHECK(!gateway_panel_init(&p,&ops,&f));gateway_panel_step(&p,&a);
 p.view=GATEWAY_PANEL_SYSTEM;p.system_page=1;step(&p,&a,&f,3);CHECK(p.system_page==2&&has(&f.last,"Display"));step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_DISPLAY);step(&p,&a,&f,2);CHECK(p.view==GATEWAY_PANEL_BACKLIGHT&&has(&f.last,"Stored: On"));
 step(&p,&a,&f,3);step(&p,&a,&f,2);CHECK(!a.configuration_transaction.candidate.settings.backlight_on&&a.configuration_transaction.state==GATEWAY_CONFIG_TX_PROMOTING);
 a.coordinator.selected=a.configuration_transaction.candidate;a.configuration_transaction.state=GATEWAY_CONFIG_TX_SUCCEEDED;a.backlight.command_known=1;a.backlight.command_on=0;
 step(&p,&a,&f,0);CHECK(p.view==GATEWAY_PANEL_DISPLAY);step(&p,&a,&f,2);CHECK(has(&f.last,"Stored: Off"));step(&p,&a,&f,1);step(&p,&a,&f,2);CHECK(a.configuration_transaction.candidate.settings.backlight_on);a.backlight.last_error=5;gateway_panel_step(&p,&a);CHECK(has(&f.last,"Command failed")&&rows_bounded(&f.last));CHECK(f.display_closes==0&&f.keypad_closes==0);gateway_panel_shutdown(&p);
}
static gateway_config_result_t stage_fail(void*x,const char*d,const gateway_persistent_config_t*c){(void)x;(void)d;(void)c;return GATEWAY_CONFIG_INVALID;}
static gateway_config_result_t promote_fail(void*x,const char*d){(void)x;(void)d;return GATEWAY_CONFIG_INVALID;}
static void web_repeat_transactions(void)
{
 unsigned int field,foreign;gateway_panel_t p;gateway_application_t a;fake_panel_t f;char token[64];
 for(field=0;field<5;field++){if(field==2||field==3)continue;
  memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);a.revision_configuration=a.coordinator.selected;
  gateway_panel_init(&p,&ops,&f);p.view=GATEWAY_PANEL_WEB;p.selected=field;gateway_application_revision(&a,p.draft_token);
  step(&p,&a,&f,2);CHECK(p.web_pending);strcpy(token,a.configuration_transaction.accepted_token);
  step(&p,&a,&f,2);CHECK(!strcmp(token,a.configuration_transaction.accepted_token)&&p.web_pending&&has(&f.last,"Saving..."));
  gateway_application_step(&a);CHECK(a.configuration_transaction.state==GATEWAY_CONFIG_TX_SUCCEEDED);
  step(&p,&a,&f,2);CHECK(p.web_pending&&!p.local_validation_failed&&strcmp(token,a.configuration_transaction.accepted_token));
  gateway_application_step(&a);gateway_panel_step(&p,&a);CHECK(!p.web_pending&&!p.local_validation_failed&&rows_bounded(&f.last));
  a.dependencies.stage_configuration=stage_fail;strcpy(token,p.draft_token);step(&p,&a,&f,2);
  CHECK(p.web_save_failed&&!p.local_validation_failed&&!p.web_pending&&!strcmp(token,p.draft_token)&&has(&f.last,"Save failed"));
  a.dependencies.stage_configuration=stage;a.dependencies.promote_configuration=promote_fail;step(&p,&a,&f,2);
  CHECK(p.web_pending);gateway_application_step(&a);CHECK(a.configuration_transaction.state==GATEWAY_CONFIG_TX_ROLLING_BACK);
  step(&p,&a,&f,2);CHECK(p.web_pending);a.configuration_transaction.port_index=8;gateway_application_step(&a);gateway_panel_step(&p,&a);
  CHECK(!p.web_pending&&p.web_save_failed&&has(&f.last,"Save failed"));
  a.dependencies.promote_configuration=promote;step(&p,&a,&f,2);CHECK(p.web_pending&&!p.local_validation_failed);
  gateway_panel_shutdown(&p);
 }
 for(foreign=0;foreign<9;foreign++){
  memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);a.revision_configuration=a.coordinator.selected;
  gateway_panel_init(&p,&ops,&f);p.view=GATEWAY_PANEL_WEB;p.selected=4;gateway_application_revision(&a,p.draft_token);
  if(foreign==0)gateway_application_revision_advance(&a);
  step(&p,&a,&f,2);
  if(foreign==0){CHECK(p.local_validation_failed&&!p.web_pending);continue;}
  if(foreign==1)gateway_application_revision_advance(&a);
  if(foreign==2){a.network.observed_valid=1;strcpy(a.network.observed.lan[0].address,"10.1.2.3");}
  gateway_application_step(&a);
  if(foreign==3)gateway_application_revision_advance(&a);
  if(foreign==4){gateway_persistent_config_t c=a.coordinator.selected;c.settings.web_interface=1;CHECK(!gateway_application_request_configuration(&a,&c));}
  if(foreign==5)strcpy(a.epoch,"other-boot");
  if(foreign==6)a.configuration_transaction.state=GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN;
  if(foreign==7){a.configuration_transaction.state=GATEWAY_CONFIG_TX_FAILED;a.configuration_transaction.rollback_failed=1;}
  if(foreign==8){p.view=GATEWAY_PANEL_NETWORK_CONFIRM;strcpy(p.draft_token,"network-draft");gateway_panel_step(&p,&a);CHECK(!p.web_pending&&!strcmp(p.draft_token,"network-draft"));continue;}
  strcpy(token,p.draft_token);step(&p,&a,&f,2);
  CHECK(p.local_validation_failed&&!p.web_pending&&!strcmp(token,p.draft_token));gateway_panel_shutdown(&p);
 }
}
static void web_protocol_menu(void)
{
 gateway_panel_t p;gateway_application_t a;fake_panel_t f;gateway_persistent_config_t old;
 memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);old=a.coordinator.selected;
 gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_SYSTEM;p.system_page=3;
 step(&p,&a,&f,GATEWAY_PANEL_KEY_F3);CHECK(p.view==GATEWAY_PANEL_WEB&&has(&f.last,"HTTPS not rec."));
 p.selected=4;step(&p,&a,&f,GATEWAY_PANEL_KEY_F3);
 CHECK(a.configuration_transaction.state==GATEWAY_CONFIG_TX_PROMOTING&&a.configuration_transaction.candidate.schema_version==3&&!a.configuration_transaction.candidate.settings.web_protocol);
 CHECK(!memcmp(old.ports,a.configuration_transaction.candidate.ports,sizeof(old.ports)));
 a.coordinator.selected=a.configuration_transaction.candidate;a.configuration_transaction.state=GATEWAY_CONFIG_TX_SUCCEEDED;
 strcpy(a.web.urls[0],"http://10.0.2.13");step(&p,&a,&f,GATEWAY_PANEL_KEY_F5);
 CHECK(p.view==GATEWAY_PANEL_WEB_URL&&has(&f.last,"LAN1 HTTP:")&&has(&f.last,"10.0.2.13")&&rows_bounded(&f.last));
 step(&p,&a,&f,GATEWAY_PANEL_KEY_F5);CHECK(p.view==GATEWAY_PANEL_WEB_URL);
 step(&p,&a,&f,GATEWAY_PANEL_KEY_F1);p.selected=4;step(&p,&a,&f,GATEWAY_PANEL_KEY_F3);CHECK(a.configuration_transaction.candidate.settings.web_protocol==1);
 gateway_panel_shutdown(&p);
}

static void stale_network_draft_cannot_claim_operation(void)
{gateway_panel_t p;gateway_application_t a;fake_panel_t f;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);gateway_panel_step(&p,&a);gateway_application_revision(&a,p.draft_token);gateway_application_revision_advance(&a);a.network_operation=7;a.network.status.state=GATEWAY_NETWORK_WAIT_CONFIRM;a.network.binding_ack=1;p.network_operation=3;p.view=GATEWAY_PANEL_NETWORK_CONFIRM;step(&p,&a,&f,GATEWAY_PANEL_KEY_F3);CHECK(p.local_validation_failed&&p.network_operation==0);step(&p,&a,&f,GATEWAY_PANEL_KEY_F3);CHECK(a.network.status.state==GATEWAY_NETWORK_WAIT_CONFIRM);gateway_panel_shutdown(&p);}

static int physical(void*x,unsigned int*m){*m=((fake_panel_t*)x)->held;return 1;}
static void held_keys(void){
 gateway_panel_t p;gateway_application_t a;fake_panel_t f;unsigned int t,value;
 test_now=0;memset(&f,0,sizeof(f));app_fixture(&a,GATEWAY_APP_READY);gateway_panel_init(&p,&ops,&f);p.key_state=physical;
 p.view=GATEWAY_PANEL_EDIT_FIELD;p.field_index=10;p.candidate=a.coordinator.selected;p.candidate.ports[0].endpoint_port=502;
 f.held=8;push(&f,3);gateway_panel_step(&p,&a);CHECK(p.candidate.ports[0].endpoint_port==503);
 test_now=499;gateway_panel_step(&p,&a);CHECK(p.candidate.ports[0].endpoint_port==503);
 test_now=500;gateway_panel_step(&p,&a);CHECK(p.candidate.ports[0].endpoint_port==504);
 for(t=510;t<=6000;t+=10){test_now=t;gateway_panel_step(&p,&a);}CHECK(p.candidate.ports[0].endpoint_port==649);
 f.held=0;test_now=6010;gateway_panel_step(&p,&a);CHECK(p.candidate.ports[0].endpoint_port==649);
 f.held=2;push(&f,1);test_now=6020;gateway_panel_step(&p,&a);CHECK(p.candidate.ports[0].endpoint_port==648);
 test_now=6520;gateway_panel_step(&p,&a);CHECK(p.candidate.ports[0].endpoint_port==647);
 for(t=6620;t<=11520;t+=100){test_now=t;gateway_panel_step(&p,&a);}
 CHECK(p.candidate.ports[0].endpoint_port==552);
 p.field_index=9;strcpy(p.candidate.ports[0].bind_address,"10.0.2.17");p.edit_octet=3;
 test_now=7000;gateway_panel_step(&p,&a);CHECK(!strcmp(p.candidate.ports[0].bind_address,"10.0.2.17"));
 f.held=0;gateway_panel_step(&p,&a);f.held=8;push(&f,3);gateway_panel_step(&p,&a);CHECK(!strcmp(p.candidate.ports[0].bind_address,"10.0.2.18"));
 p.edit_octet=2;test_now=8000;gateway_panel_step(&p,&a);CHECK(!strcmp(p.candidate.ports[0].bind_address,"10.0.2.18"));
 f.held=10;test_now=9000;gateway_panel_step(&p,&a);CHECK(!strcmp(p.candidate.ports[0].bind_address,"10.0.2.18"));
 f.held=0;gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_APPLY_CONFIRM;f.held=8;
 for(t=0;t<20;t++){test_now+=500;gateway_panel_step(&p,&a);}CHECK(p.view==GATEWAY_PANEL_APPLY_CONFIRM);
 f.held=0;gateway_panel_step(&p,&a);p.view=GATEWAY_PANEL_MENU;p.selected=0;f.held=8;gateway_panel_step(&p,&a);CHECK(p.selected==0);
 f.held=0;gateway_panel_step(&p,&a);f.held=8;push(&f,3);gateway_panel_step(&p,&a);CHECK(p.selected==1);
 value=p.selected;test_now+=500;gateway_panel_step(&p,&a);CHECK(p.selected==value+1);
 f.held=0;gateway_panel_step(&p,&a);value=p.selected;push(&f,3);gateway_panel_step(&p,&a);CHECK(p.selected==(value+1)%7);
 gateway_panel_shutdown(&p);
}

static void shutdown_result_keys(void){gateway_application_t a;gateway_panel_t p;fake_panel_t f;app_fixture(&a,GATEWAY_APP_READY);memset(&f,0,sizeof(f));gateway_panel_init(&p,&ops,&f);
 p.view=GATEWAY_PANEL_SHUTDOWN_CONFIRM;gateway_application_revision(&a,p.draft_token);push(&f,2);gateway_panel_step(&p,&a);
 CHECK(a.run_control.stop_requested);CHECK(p.view==GATEWAY_PANEL_STOPPING);CHECK(!strcmp(f.last.row[0],"Stopping Gateway"));
 a.process_state=GATEWAY_PROCESS_STOPPED;gateway_panel_stop_result(&p,2);gateway_panel_step(&p,&a);CHECK(!strncmp(f.last.row[0],"Shutdown failed",15));CHECK(!strncmp(f.last.row[7],"F1 Back",7));
 push(&f,0);gateway_panel_step(&p,&a);CHECK(p.view==GATEWAY_PANEL_MENU);p.selected=5;push(&f,2);gateway_panel_step(&p,&a);CHECK(p.view==GATEWAY_PANEL_STOP_FAILED);
 gateway_panel_stop_result(&p,0);gateway_panel_step(&p,&a);CHECK(!strncmp(f.last.row[0],"Gateway stopped",15));CHECK(!strcmp(f.last.row[4],"OS still running"));gateway_panel_shutdown(&p);}
int main(void){shutdown_result_keys();held_keys();web_repeat_transactions();web_protocol_menu();stale_network_draft_cannot_claim_operation();backlight_menu();ntp_test_keys_and_cancel();time_refresh_wrap();unavailable_and_failure_isolation();startup_home_and_dirty();help_and_main_menu_layout();navigation_and_views();safe_mode_system_diagnostics_events();system_time_view_edit_and_failures();editing_confirmation_and_results();shutdown_confirmation_and_bounds();title_dictionary_and_bounds();fixed_uart_and_local_validation();startup_port_result_and_fatal();ntp_settings_edit_cancel_and_apply();printf("panel checks=%u failed=%u panel=%u screen=%u moxa=%u\n",checks,failures,gateway_panel_memory_bytes(),gateway_panel_screen_bytes(),(unsigned int)sizeof(gateway_panel_moxa_t));return failures?1:0;}
