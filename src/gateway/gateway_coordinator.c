#include <string.h>

#include "gateway/gateway_coordinator.h"

static gateway_config_result_t select_real(void *context,const char *directory,gateway_persistent_config_t *config,gateway_config_source_t *source)
{(void)context;return gateway_config_startup_select(directory,config,source);}
static int controller_init(gateway_controller_t*c,const gateway_port_config_t*p,const gateway_port_binding_t*b){return gateway_controller_init(c,p,b);}
static void controller_start(gateway_controller_t*c,core_tick_t n){gateway_controller_start(c,n);}
static void controller_step(gateway_controller_t*c,core_tick_t n){gateway_controller_step(c,n);}
static void controller_shutdown(gateway_controller_t*c,core_tick_t n){gateway_controller_shutdown(c,n);}
static void controller_health(const gateway_controller_t*c,gateway_health_t*h){gateway_controller_health(c,h);}
static const gateway_controller_driver_t default_driver={controller_init,controller_start,controller_step,controller_shutdown,controller_health};

static unsigned int saturating_add(unsigned int a,unsigned int b){return 0xffffffffU-a<b?0xffffffffU:a+b;}
static unsigned int percent(unsigned int n){return n>=GATEWAY_STARTUP_PROGRESS_TOTAL?100U:(n*100U)/GATEWAY_STARTUP_PROGRESS_TOTAL;}
static void emit(gateway_coordinator_t*c,gateway_application_state_t stage,gateway_startup_event_result_t result,unsigned int port,gateway_application_error_t error)
{gateway_startup_event_t*e;unsigned int slot;if(c->event_count<GATEWAY_STARTUP_EVENT_CAPACITY){slot=(c->event_head+c->event_count)%GATEWAY_STARTUP_EVENT_CAPACITY;++c->event_count;}else{slot=c->event_head;c->event_head=(c->event_head+1U)%GATEWAY_STARTUP_EVENT_CAPACITY;}e=&c->events[slot];memset(e,0,sizeof(*e));if(c->event_sequence!=0xffffffffU)++c->event_sequence;e->sequence=c->event_sequence;e->stage=stage;e->result=result;e->progress_numerator=c->progress_numerator;e->progress_denominator=GATEWAY_STARTUP_PROGRESS_TOTAL;e->progress_percent=percent(c->progress_numerator);e->port_index=port;e->error=error;}
static void advance(gateway_coordinator_t*c,gateway_application_state_t next,gateway_startup_event_result_t result)
{if(c->progress_numerator<GATEWAY_STARTUP_PROGRESS_TOTAL)++c->progress_numerator;c->state=next;emit(c,next,result,GATEWAY_NO_PORT,c->error);}
static int terminal_port(gateway_port_lifecycle_t s){return s==GATEWAY_PORT_READY||s==GATEWAY_PORT_NETWORK_WAIT||s==GATEWAY_PORT_DISABLED||s==GATEWAY_PORT_DEGRADED||s==GATEWAY_PORT_ERROR;}
static gateway_application_error_t config_error(gateway_config_result_t r){if(r==GATEWAY_CONFIG_IO_ERROR)return GATEWAY_APP_ERROR_STORAGE_IO;if(r==GATEWAY_CONFIG_UNSUPPORTED_VERSION)return GATEWAY_APP_ERROR_CONFIG_UNSUPPORTED;return GATEWAY_APP_ERROR_CONFIG_INVALID;}

void gateway_run_control_init(gateway_run_control_t*c){if(c)c->stop_requested=0;}
void gateway_run_control_request_stop(gateway_run_control_t*c){if(c)c->stop_requested=1;}
const gateway_controller_driver_t *gateway_default_controller_driver(void){return &default_driver;}

int gateway_coordinator_init(gateway_coordinator_t*c,const char*d,gateway_run_control_t*r,gateway_configuration_select_fn select,void*select_context,gateway_binding_provider_fn provider,gateway_binding_attach_fn attach,void*binding_context,const gateway_controller_driver_t*driver,core_tick_t now)
{const gateway_controller_driver_t*chosen=driver?driver:&default_driver;if(!c||!d||!provider||!chosen->init||!chosen->start||!chosen->step||!chosen->shutdown||!chosen->health)return-1;memset(c,0,sizeof(*c));c->configuration_directory=d;c->run_control=r;c->select_configuration=select?select:select_real;c->select_context=select_context;c->provide_bindings=provider;c->attach_bindings=attach;c->binding_context=binding_context;c->controller_driver=chosen;c->state=GATEWAY_APP_BOOTSTRAP;c->config_source=GATEWAY_CONFIG_SOURCE_SAFE_MODE;c->persistence_result=GATEWAY_CONFIG_INVALID;c->started_at=now;emit(c,c->state,GATEWAY_EVENT_BEGIN,GATEWAY_NO_PORT,GATEWAY_APP_ERROR_NONE);return 0;}

static void begin_shutdown(gateway_coordinator_t*c,core_tick_t now)
{if(c->state==GATEWAY_APP_STOPPED||c->state==GATEWAY_APP_SHUTTING_DOWN)return;c->state=GATEWAY_APP_SHUTTING_DOWN;c->shutdown_deadline=now+GATEWAY_COORDINATOR_SHUTDOWN_TIMEOUT;emit(c,c->state,GATEWAY_EVENT_BEGIN,GATEWAY_NO_PORT,c->error);if(c->controller_initialized&&!c->controller_shutdown_called){c->controller_driver->shutdown(&c->controller,now);c->controller_shutdown_called=1;}if(!c->controller_initialized){c->cleanup_complete=1;c->state=GATEWAY_APP_STOPPED;emit(c,c->state,GATEWAY_EVENT_COMPLETE,GATEWAY_NO_PORT,c->error);}}
void gateway_coordinator_request_shutdown(gateway_coordinator_t*c,core_tick_t now){if(c)begin_shutdown(c,now);}

static void startup_ports(gateway_coordinator_t*c,core_tick_t now)
{unsigned int i,bit;c->controller_driver->step(&c->controller,now);for(i=0;i<GATEWAY_PORT_COUNT;++i){bit=1U<<i;if((c->startup_ports_complete&bit)==0U&&terminal_port(c->controller.ports[i].lifecycle)){c->startup_ports_complete|=bit;if(c->progress_numerator<13U)++c->progress_numerator;emit(c,GATEWAY_APP_STARTING_PORTS,c->controller.ports[i].lifecycle==GATEWAY_PORT_READY?GATEWAY_EVENT_OK:(c->controller.ports[i].lifecycle==GATEWAY_PORT_DISABLED?GATEWAY_EVENT_SKIPPED:GATEWAY_EVENT_FAILED),i,c->controller.ports[i].lifecycle==GATEWAY_PORT_ERROR||c->controller.ports[i].lifecycle==GATEWAY_PORT_DEGRADED?GATEWAY_APP_ERROR_PORT_START:GATEWAY_APP_ERROR_NONE);}}if(c->startup_ports_complete==0xffU){c->state=GATEWAY_APP_CHECKING_HEALTH;emit(c,c->state,GATEWAY_EVENT_BEGIN,GATEWAY_NO_PORT,c->error);}}

void gateway_coordinator_step(gateway_coordinator_t*c,core_tick_t now)
{
    gateway_error_t errors[GATEWAY_PORT_COUNT];
    gateway_health_t h;
    unsigned int i,unfinished=0;
    if(!c)return;
    if(c->run_control&&c->run_control->stop_requested)begin_shutdown(c,now);
    switch(c->state){
    case GATEWAY_APP_BOOTSTRAP:
        advance(c,GATEWAY_APP_LOADING_CONFIGURATION,GATEWAY_EVENT_OK);break;
    case GATEWAY_APP_LOADING_CONFIGURATION:
        c->persistence_result=c->select_configuration(c->select_context,c->configuration_directory,&c->selected,&c->config_source);
        if(c->persistence_result!=GATEWAY_CONFIG_OK&&c->persistence_result!=GATEWAY_CONFIG_ABSENT){c->error=config_error(c->persistence_result);gateway_persistent_safe_mode(&c->selected);c->config_source=GATEWAY_CONFIG_SOURCE_SAFE_MODE;}
        advance(c,GATEWAY_APP_SELECTING_CONFIGURATION,c->config_source==GATEWAY_CONFIG_SOURCE_BACKUP?GATEWAY_EVENT_RECOVERED:GATEWAY_EVENT_OK);break;
    case GATEWAY_APP_SELECTING_CONFIGURATION:
        advance(c,GATEWAY_APP_VALIDATING_CONFIGURATION,GATEWAY_EVENT_OK);break;
    case GATEWAY_APP_VALIDATING_CONFIGURATION:
        if(gateway_configuration_validate(c->selected.ports,errors)!=0){c->error=GATEWAY_APP_ERROR_CONFIG_INVALID;c->config_source=GATEWAY_CONFIG_SOURCE_SAFE_MODE;gateway_persistent_safe_mode(&c->selected);}
        advance(c,GATEWAY_APP_INITIALIZING_CONTROLLER,c->config_source==GATEWAY_CONFIG_SOURCE_SAFE_MODE?GATEWAY_EVENT_RECOVERED:GATEWAY_EVENT_OK);break;
    case GATEWAY_APP_INITIALIZING_CONTROLLER:
        memset(c->bindings,0,sizeof(c->bindings));
        if(c->provide_bindings(c->binding_context,c->selected.ports,c->bindings)!=0){c->error=GATEWAY_APP_ERROR_BINDING_INIT;c->state=GATEWAY_APP_FATAL_ERROR;c->progress_numerator=GATEWAY_STARTUP_PROGRESS_TOTAL;emit(c,c->state,GATEWAY_EVENT_FAILED,GATEWAY_NO_PORT,c->error);break;}
        if(c->controller_driver->init(&c->controller,c->selected.ports,c->bindings)!=0){c->error=GATEWAY_APP_ERROR_CONTROLLER_INIT;c->state=GATEWAY_APP_FATAL_ERROR;c->progress_numerator=GATEWAY_STARTUP_PROGRESS_TOTAL;emit(c,c->state,GATEWAY_EVENT_FAILED,GATEWAY_NO_PORT,c->error);break;}
        c->controller_initialized=1;
        if(c->attach_bindings&&c->attach_bindings(c->binding_context,&c->controller)!=0){c->error=GATEWAY_APP_ERROR_BINDING_INIT;c->state=GATEWAY_APP_FATAL_ERROR;c->progress_numerator=GATEWAY_STARTUP_PROGRESS_TOTAL;emit(c,c->state,GATEWAY_EVENT_FAILED,GATEWAY_NO_PORT,c->error);break;}
        c->controller_driver->start(&c->controller,now);c->controller_start_called=1;advance(c,GATEWAY_APP_STARTING_PORTS,GATEWAY_EVENT_OK);break;
    case GATEWAY_APP_STARTING_PORTS:
        startup_ports(c,now);break;
    case GATEWAY_APP_CHECKING_HEALTH:
        c->controller_driver->health(&c->controller,&h);c->progress_numerator=GATEWAY_STARTUP_PROGRESS_TOTAL;
        if(c->config_source==GATEWAY_CONFIG_SOURCE_SAFE_MODE)c->state=GATEWAY_APP_SAFE_MODE;
        else if(h.enabled_ports==h.ready_ports)c->state=GATEWAY_APP_READY;
        else{c->state=GATEWAY_APP_DEGRADED;c->error=GATEWAY_APP_ERROR_PORT_START;}
        emit(c,c->state,GATEWAY_EVENT_COMPLETE,GATEWAY_NO_PORT,c->error);break;
    case GATEWAY_APP_READY:case GATEWAY_APP_DEGRADED:case GATEWAY_APP_SAFE_MODE:
        c->controller_driver->step(&c->controller,now);break;
    case GATEWAY_APP_SHUTTING_DOWN:
        if(c->controller_initialized)c->controller_driver->step(&c->controller,now);
        for(i=0;i<GATEWAY_PORT_COUNT;++i)if(c->controller.ports[i].lifecycle==GATEWAY_PORT_STOPPING||c->controller.ports[i].listener_owned)unfinished=1;
        if(!unfinished){c->cleanup_complete=1;c->state=GATEWAY_APP_STOPPED;emit(c,c->state,GATEWAY_EVENT_COMPLETE,GATEWAY_NO_PORT,c->error);}
        else if((long)(now-c->shutdown_deadline)>=0){c->error=GATEWAY_APP_ERROR_SHUTDOWN_INCOMPLETE;c->state=GATEWAY_APP_STOPPED;emit(c,c->state,GATEWAY_EVENT_FAILED,GATEWAY_NO_PORT,c->error);}
        break;
    default:break;
    }
}

void gateway_coordinator_health(const gateway_coordinator_t*c,core_tick_t now,gateway_coordinator_health_t*h)
{unsigned int i;if(!c||!h)return;memset(h,0,sizeof(*h));h->state=c->state;h->config_source=c->config_source;h->persistence_result=c->persistence_result;h->error=c->error;h->progress_percent=percent(c->progress_numerator);h->started_at=c->started_at;h->uptime=now-c->started_at;h->controller_initialized=c->controller_initialized;h->cleanup_complete=c->cleanup_complete;if(c->event_count)gateway_coordinator_event(c,c->event_count-1U,&h->last_event);if(!c->controller_initialized)return;c->controller_driver->health(&c->controller,&h->gateway);h->enabled_ports=h->gateway.enabled_ports;h->ready_ports=h->gateway.ready_ports;h->clients=h->gateway.active_clients;h->accepted=h->gateway.accepted_transactions;h->completed=h->gateway.completed_transactions;for(i=0;i<GATEWAY_PORT_COUNT;++i){const gateway_port_health_t*p=&h->gateway.port[i];if(p->lifecycle==GATEWAY_PORT_DISABLED)++h->disabled_ports;if(p->lifecycle==GATEWAY_PORT_ERROR||p->lifecycle==GATEWAY_PORT_DEGRADED)++h->error_ports;h->timeouts=saturating_add(h->timeouts,p->transactions.timeouts);h->recoveries=saturating_add(h->recoveries,p->transactions.recoveries);h->stale_responses=saturating_add(h->stale_responses,p->transactions.stale_responses);}}
unsigned int gateway_coordinator_event_count(const gateway_coordinator_t*c){return c?c->event_count:0U;}
int gateway_coordinator_event(const gateway_coordinator_t*c,unsigned int index,gateway_startup_event_t*e){unsigned int slot;if(!c||!e||index>=c->event_count)return-1;slot=(c->event_head+index)%GATEWAY_STARTUP_EVENT_CAPACITY;*e=c->events[slot];return 0;}
unsigned int gateway_coordinator_memory_bytes(void){return(unsigned int)sizeof(gateway_coordinator_t);}
