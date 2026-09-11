#include <string.h>
#include <stdio.h>

#include "gateway/gateway_controller.h"

static void increment(unsigned int *value)
{ if (*value != 0xffffffffU) ++*value; }
static void add_saturating(unsigned int *value,unsigned int amount)
{ if(0xffffffffU-*value<amount)*value=0xffffffffU;else *value+=amount; }

static int standard_baud(unsigned long value)
{
    int i;
    for (i = 0; i < FOURVRS_BAUD_COUNT; ++i)
        if (baud_value(i) == value) return 1;
    return 0;
}

static int bind_address_valid(const char *s)
{
    unsigned int a,b,c,d; char tail;
    if (!s || strlen(s) == 0U || strlen(s) >= GATEWAY_BIND_ADDRESS_LENGTH) return 0;
    return sscanf(s,"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)==4 &&
           a<=255U && b<=255U && c<=255U && d<=255U;
}

static gateway_error_t validate_one(const gateway_port_config_t *config)
{
    if (config == 0 || config->uart_index >= GATEWAY_PORT_COUNT ||
        config->mode >= SERIAL_MODE_COUNT ||
        config->data_bits < 5U || config->data_bits > 8U ||
        config->parity >= PARITY_COUNT ||
        (config->stop_bits != 1U && config->stop_bits != 2U) ||
        config->revision == 0U || (config->enabled != 0 && config->enabled != 1) ||
        (config->special_baud_enabled != 0 && config->special_baud_enabled != 1) ||
        !bind_address_valid(config->bind_address))
        return GATEWAY_ERROR_INVALID_CONFIG;
    if ((!config->special_baud_enabled && !standard_baud(config->baud)) ||
        (config->special_baud_enabled && (config->special_baud == 0UL ||
         config->special_baud > 0xffffffffUL)))
        return GATEWAY_ERROR_INVALID_CONFIG;
    if (config->bind_policy>3U) return GATEWAY_ERROR_INVALID_CONFIG;
    if (!config->enabled) return GATEWAY_ERROR_NONE;
    if (config->transport != TRANSPORT_MODBUS_TCP &&
        config->transport != TRANSPORT_MODBUS_UDP && config->transport != TRANSPORT_RTU_UDP &&
        config->transport != TRANSPORT_RAW_TCP && config->transport != TRANSPORT_RAW_UDP)
        return GATEWAY_ERROR_UNSUPPORTED_TRANSPORT;
    if (config->endpoint_port == 0U || config->endpoint_port > 65535U)
        return GATEWAY_ERROR_INVALID_CONFIG;
    return GATEWAY_ERROR_NONE;
}

void gateway_configuration_defaults(gateway_port_config_t ports[GATEWAY_PORT_COUNT])
{
    unsigned int i;
    memset(ports, 0, sizeof(gateway_port_config_t) * GATEWAY_PORT_COUNT);
    for (i = 0; i < GATEWAY_PORT_COUNT; ++i) {
        ports[i].enabled = 1; ports[i].uart_index = i;
        ports[i].mode = SERIAL_MODE_RS232; ports[i].baud = 9600UL;
        ports[i].data_bits = 8; ports[i].parity = PARITY_NONE;
        ports[i].stop_bits = 1; ports[i].transport = TRANSPORT_MODBUS_TCP;
        memcpy(ports[i].bind_address,"0.0.0.0",8U);
        ports[i].endpoint_port = 502U + i; ports[i].revision = 1;
    }
}

int gateway_configuration_validate(const gateway_port_config_t ports[GATEWAY_PORT_COUNT],
                                   gateway_error_t errors[GATEWAY_PORT_COUNT])
{
    unsigned int i, j; int failures = 0;
    if (ports == 0 || errors == 0) return -1;
    for (i = 0; i < GATEWAY_PORT_COUNT; ++i) errors[i] = validate_one(&ports[i]);
    for (i = 0; i < GATEWAY_PORT_COUNT; ++i) if (ports[i].enabled) {
        for (j = i + 1U; j < GATEWAY_PORT_COUNT; ++j) if (ports[j].enabled) {
            if (ports[i].uart_index == ports[j].uart_index) {
                errors[i] = errors[j] = GATEWAY_ERROR_DUPLICATE_UART;
            }
            if ((((ports[i].transport == TRANSPORT_MODBUS_TCP || ports[i].transport == TRANSPORT_RAW_TCP) &&
                  (ports[j].transport == TRANSPORT_MODBUS_TCP || ports[j].transport == TRANSPORT_RAW_TCP)) ||
                 ((ports[i].transport == TRANSPORT_MODBUS_UDP || ports[i].transport == TRANSPORT_RTU_UDP || ports[i].transport == TRANSPORT_RAW_UDP) &&
                  (ports[j].transport == TRANSPORT_MODBUS_UDP || ports[j].transport == TRANSPORT_RTU_UDP || ports[j].transport == TRANSPORT_RAW_UDP))) &&
                ports[i].endpoint_port == ports[j].endpoint_port &&
                (strcmp(ports[i].bind_address,ports[j].bind_address)==0 ||
                 strcmp(ports[i].bind_address,"0.0.0.0")==0 ||
                 strcmp(ports[j].bind_address,"0.0.0.0")==0)) {
                errors[i] = errors[j] = GATEWAY_ERROR_DUPLICATE_ENDPOINT;
            }
        }
    }
    for (i = 0; i < GATEWAY_PORT_COUNT; ++i) if (errors[i] != GATEWAY_ERROR_NONE) ++failures;
    return failures;
}

static core_port_config_t core_config(const gateway_port_config_t *config)
{
    core_port_config_t c; memset(&c, 0, sizeof(c));
    c.revision=config->revision; c.mode=(unsigned int)config->mode;
    c.baud=(unsigned int)(config->special_baud_enabled?config->special_baud:config->baud);
    c.data_bits=config->data_bits; c.parity=(unsigned int)config->parity;
    c.stop_bits=config->stop_bits; c.special_baud_enabled=(unsigned int)config->special_baud_enabled;
    c.special_baud=(unsigned int)config->special_baud; c.transport=(unsigned int)config->transport;
    c.endpoint_port=config->endpoint_port; return c;
}

int gateway_controller_init(gateway_controller_t *controller,
                            const gateway_port_config_t ports[GATEWAY_PORT_COUNT],
                            const gateway_port_binding_t bindings[GATEWAY_PORT_COUNT])
{
    gateway_error_t errors[GATEWAY_PORT_COUNT]; unsigned int i; int failures;
    if (controller==0 || ports==0 || bindings==0) return -1;
    memset(controller,0,sizeof(*controller)); failures=gateway_configuration_validate(ports,errors);
    for(i=0;i<GATEWAY_PORT_COUNT;++i){gateway_port_controller_t*p=&controller->ports[i];core_port_config_t c=core_config(&ports[i]);
        p->current=p->known_good=ports[i];p->binding=bindings[i];p->config_generation=ports[i].revision;
        port_runtime_init(&p->runtime,i,&c,bindings[i].backend_ops,bindings[i].backend_context);
        p->last_error=errors[i];p->lifecycle=errors[i]==GATEWAY_ERROR_NONE?GATEWAY_PORT_DISABLED:GATEWAY_PORT_ERROR;
    }
    return failures;
}

static int transport_start_at(gateway_port_controller_t *p,
                              const gateway_port_config_t *config)
{
    if(p->binding.transport_ops==0 || p->binding.transport_ops->start==0) return -1;
    if(p->binding.transport_ops->start(p->binding.transport_context,config)!=0)return -1;
    p->listener_owned=1;return 0;
}
static int transport_start(gateway_port_controller_t *p)
{ return transport_start_at(p, &p->current); }
static int transport_stop(gateway_port_controller_t*p)
{
    int r=0;if(p->listener_owned&&p->binding.transport_ops&&p->binding.transport_ops->stop)
        r=p->binding.transport_ops->stop(p->binding.transport_context);
    if(r==0)p->listener_owned=0;
    return r;
}

void gateway_controller_start(gateway_controller_t *controller, core_tick_t now)
{
    unsigned int i;if(!controller)return;
    for(i=0;i<GATEWAY_PORT_COUNT;++i){gateway_port_controller_t*p=&controller->ports[i];
        if(p->lifecycle!=GATEWAY_PORT_DISABLED||!p->current.enabled)continue;
        if(port_runtime_start(&p->runtime,now,GATEWAY_START_TIMEOUT)==0)p->lifecycle=GATEWAY_PORT_STARTING;
        else{p->lifecycle=GATEWAY_PORT_ERROR;p->last_error=GATEWAY_ERROR_BACKEND_START;increment(&p->startup_failures);}
    }
}

static void start_listener_or_fail(gateway_port_controller_t*p,core_tick_t now)
{
    if(p->network_wait){p->lifecycle=GATEWAY_PORT_NETWORK_WAIT;return;}
    if(transport_start(p)==0){p->lifecycle=GATEWAY_PORT_READY;p->last_error=GATEWAY_ERROR_NONE;return;}
    increment(&p->listener_failures);p->last_error=GATEWAY_ERROR_LISTENER_START;
    if(port_runtime_request_stop(&p->runtime,now,GATEWAY_STOP_TIMEOUT)==0)p->lifecycle=GATEWAY_PORT_STOPPING;
    else p->lifecycle=GATEWAY_PORT_ERROR;
}

static void step_port(gateway_port_controller_t*p,core_tick_t now)
{
    increment(&p->scheduler_steps);
    if(p->listener_owned&&p->binding.transport_ops&&p->binding.transport_ops->step)
        p->binding.transport_ops->step(p->binding.transport_context,now);
    port_runtime_step(&p->runtime,now);
    if(p->lifecycle==GATEWAY_PORT_STARTING){
        if(p->runtime.state==PORT_READY)start_listener_or_fail(p,now);
        else if(p->runtime.state==PORT_DEGRADED||p->runtime.state==PORT_ERROR){p->lifecycle=GATEWAY_PORT_ERROR;p->last_error=GATEWAY_ERROR_BACKEND_START;increment(&p->startup_failures);}
    }else if(p->lifecycle==GATEWAY_PORT_READY){
        if(p->runtime.state==PORT_DEGRADED||p->runtime.state==PORT_ERROR)p->lifecycle=GATEWAY_PORT_DEGRADED;
    }else if(p->lifecycle==GATEWAY_PORT_RECONFIGURING){
        if(p->listener_rebind){
            if(p->runtime.state==PORT_READY){
                p->listener_rebind=0;
                if(p->network_wait){p->lifecycle=GATEWAY_PORT_NETWORK_WAIT;return;}
                if(transport_start_at(p,&p->staged)==0){
                    p->current=p->known_good=p->staged;
                    p->runtime.current_config.revision=p->staged.revision;
                    p->runtime.known_good_config.revision=p->staged.revision;
                    increment(&p->config_generation);p->lifecycle=GATEWAY_PORT_READY;
                    p->last_error=GATEWAY_ERROR_NONE;
                }else{
                    increment(&p->listener_failures);increment(&p->reconfigure_failures);
                    p->last_error=GATEWAY_ERROR_LISTENER_START;
                    if(transport_start(p)==0)p->lifecycle=GATEWAY_PORT_READY;
                    else{p->lifecycle=GATEWAY_PORT_ERROR;increment(&p->rollback_failures);}
                }
            }else if(p->runtime.state==PORT_ERROR||p->runtime.state==PORT_DEGRADED){
                p->listener_rebind=0;p->lifecycle=GATEWAY_PORT_ERROR;
                p->last_error=GATEWAY_ERROR_RECONFIGURE;increment(&p->reconfigure_failures);
            }
            return;
        }
        if(p->runtime.state==PORT_READY){
            if(p->runtime.current_config.revision!=p->staged.revision){p->last_error=GATEWAY_ERROR_RECONFIGURE;increment(&p->reconfigure_failures);start_listener_or_fail(p,now);}
            else{if(p->network_wait){p->current=p->known_good=p->staged;increment(&p->config_generation);p->lifecycle=GATEWAY_PORT_NETWORK_WAIT;p->last_error=GATEWAY_ERROR_NONE;return;}
            if(transport_start_at(p,&p->staged)==0){p->current=p->known_good=p->staged;increment(&p->config_generation);p->lifecycle=GATEWAY_PORT_READY;p->last_error=GATEWAY_ERROR_NONE;}else{
                core_port_config_t old=core_config(&p->known_good);increment(&p->listener_failures);p->last_error=GATEWAY_ERROR_LISTENER_START;
                if(port_runtime_request_reconfigure(&p->runtime,&old,now,GATEWAY_RECONFIGURE_TIMEOUT)==0)p->lifecycle=GATEWAY_PORT_ROLLING_BACK;else p->lifecycle=GATEWAY_PORT_ERROR;}}
        }else if(p->runtime.state==PORT_ERROR){p->lifecycle=GATEWAY_PORT_ERROR;p->last_error=GATEWAY_ERROR_ROLLBACK;increment(&p->rollback_failures);}
    }else if(p->lifecycle==GATEWAY_PORT_ROLLING_BACK){
        if(p->runtime.state==PORT_READY){if(p->network_wait){p->lifecycle=GATEWAY_PORT_NETWORK_WAIT;increment(&p->reconfigure_failures);return;}
        if(transport_start(p)==0){p->lifecycle=GATEWAY_PORT_READY;increment(&p->reconfigure_failures);}else{p->lifecycle=GATEWAY_PORT_ERROR;increment(&p->rollback_failures);}}
        else if(p->runtime.state==PORT_ERROR){p->lifecycle=GATEWAY_PORT_ERROR;increment(&p->rollback_failures);}
    }else if(p->lifecycle==GATEWAY_PORT_STOPPING){
        if(p->runtime.state==PORT_DISABLED){if(p->pending_disable){p->current=p->known_good=p->staged;increment(&p->config_generation);p->lifecycle=GATEWAY_PORT_DISABLED;}else p->lifecycle=p->last_error==GATEWAY_ERROR_NONE?GATEWAY_PORT_DISABLED:GATEWAY_PORT_ERROR;p->pending_disable=0;}
        else if(p->runtime.state==PORT_ERROR){p->lifecycle=GATEWAY_PORT_ERROR;p->last_error=GATEWAY_ERROR_STOP;increment(&p->stop_failures);}
    }
}

void gateway_controller_step(gateway_controller_t *controller, core_tick_t now)
{
    unsigned int n;if(!controller)return;increment(&controller->scheduler_steps);
    for(n=0;n<GATEWAY_PORT_COUNT;++n){unsigned int i=(controller->next_port+n)%GATEWAY_PORT_COUNT;step_port(&controller->ports[i],now);}
    controller->next_port=(controller->next_port+1U)%GATEWAY_PORT_COUNT;
}

static int validate_replacement(gateway_controller_t*c,unsigned int index,const gateway_port_config_t*cfg)
{
    gateway_port_config_t all[GATEWAY_PORT_COUNT];gateway_error_t errors[GATEWAY_PORT_COUNT];unsigned int i;
    for(i=0;i<GATEWAY_PORT_COUNT;++i)
        all[i]=c->ports[i].current;
    all[index]=*cfg;
    gateway_configuration_validate(all,errors);return errors[index]==GATEWAY_ERROR_NONE?0:-1;
}

int gateway_controller_reconfigure(gateway_controller_t*c,unsigned int index,const gateway_port_config_t*cfg,core_tick_t now)
{
    gateway_port_controller_t*p;core_port_config_t core;
    if(!c||!cfg||index>=GATEWAY_PORT_COUNT||validate_replacement(c,index,cfg)!=0)
        return -1;
    p=&c->ports[index];
    if(p->lifecycle!=GATEWAY_PORT_READY&&p->lifecycle!=GATEWAY_PORT_DISABLED&&p->lifecycle!=GATEWAY_PORT_NETWORK_WAIT)return -1;
    if(!cfg->enabled){if(p->lifecycle==GATEWAY_PORT_DISABLED){p->current=*cfg;increment(&p->config_generation);return 0;}if(transport_stop(p)!=0){p->last_error=GATEWAY_ERROR_STOP;increment(&p->stop_failures);return-1;}p->staged=*cfg;p->pending_disable=1;p->last_error=GATEWAY_ERROR_NONE;return port_runtime_request_stop(&p->runtime,now,GATEWAY_STOP_TIMEOUT)==0?(p->lifecycle=GATEWAY_PORT_STOPPING,0):-1;}
    if(p->lifecycle==GATEWAY_PORT_DISABLED){p->current=p->known_good=*cfg;core=core_config(cfg);port_runtime_init(&p->runtime,index,&core,p->binding.backend_ops,p->binding.backend_context);increment(&p->config_generation);return port_runtime_start(&p->runtime,now,GATEWAY_START_TIMEOUT)==0?(p->lifecycle=GATEWAY_PORT_STARTING,0):-1;}
    {
        gateway_port_config_t same=*cfg;
        memcpy(same.bind_address,p->current.bind_address,sizeof(same.bind_address));
        same.revision=p->current.revision;
        same.bind_policy=p->current.bind_policy;
        if(!memcmp(&same,&p->current,sizeof(same))){
            if(transport_stop(p)!=0){p->last_error=GATEWAY_ERROR_STOP;increment(&p->stop_failures);return -1;}
            if(port_runtime_detach_listener(&p->runtime,now)){p->lifecycle=GATEWAY_PORT_ERROR;return -1;}
            p->staged=*cfg;p->listener_rebind=1;p->lifecycle=GATEWAY_PORT_RECONFIGURING;
            return 0;
        }
    }
    if(transport_stop(p)!=0){p->last_error=GATEWAY_ERROR_STOP;increment(&p->stop_failures);return-1;}p->staged=*cfg;core=core_config(cfg);p->last_error=GATEWAY_ERROR_NONE;
    if(port_runtime_request_reconfigure(&p->runtime,&core,now,GATEWAY_RECONFIGURE_TIMEOUT)!=0){transport_start(p);return -1;}
    p->lifecycle=GATEWAY_PORT_RECONFIGURING;return 0;
}

int gateway_controller_network_wait(gateway_controller_t *c,unsigned int index,unsigned int wait,core_tick_t now)
{
    gateway_port_controller_t *p;if(!c||index>=GATEWAY_PORT_COUNT||wait>1U)return -1;p=&c->ports[index];
    p->network_wait=wait;
    if(wait&&p->lifecycle==GATEWAY_PORT_READY){
        if(transport_stop(p)||port_runtime_detach_listener(&p->runtime,now))return -1;
        p->lifecycle=GATEWAY_PORT_NETWORK_WAIT;
    }
    return 0;
}
void gateway_controller_shutdown(gateway_controller_t*c,core_tick_t now)
{
    unsigned int i;if(!c)return;for(i=0;i<GATEWAY_PORT_COUNT;++i){gateway_port_controller_t*p=&c->ports[i];
        if(transport_stop(p)!=0){p->last_error=GATEWAY_ERROR_STOP;increment(&p->stop_failures);}else p->last_error=GATEWAY_ERROR_NONE;if(p->runtime.state!=PORT_DISABLED&&p->runtime.state!=PORT_STOPPING){if(port_runtime_request_stop(&p->runtime,now,GATEWAY_STOP_TIMEOUT)==0)p->lifecycle=GATEWAY_PORT_STOPPING;else{p->lifecycle=GATEWAY_PORT_ERROR;p->last_error=GATEWAY_ERROR_STOP;}}}
}

void gateway_controller_health(const gateway_controller_t*c,gateway_health_t*h)
{
    unsigned int i;if(!c||!h)return;memset(h,0,sizeof(*h));
    for(i=0;i<GATEWAY_PORT_COUNT;++i){const gateway_port_controller_t*p=&c->ports[i];gateway_port_health_t*q=&h->port[i];unsigned int clients=0;
        q->port_number=i+1U;q->enabled=p->current.enabled;q->lifecycle=p->lifecycle;q->last_error=p->last_error;q->config=p->current;q->runtime_state=p->runtime.state;q->transactions=p->runtime.stats;q->config_generation=p->config_generation;
        if(p->listener_owned&&p->binding.transport_ops&&p->binding.transport_ops->client_count)
            clients=p->binding.transport_ops->client_count(p->binding.transport_context);
        q->connected_clients=clients;
        if(p->binding.transport_ops&&p->binding.transport_ops->crc_errors)
            q->crc_errors=p->binding.transport_ops->crc_errors(p->binding.transport_context);
        if(p->binding.transport_ops&&p->binding.transport_ops->framing_errors)
            q->framing_errors=p->binding.transport_ops->framing_errors(p->binding.transport_context);
        if(p->binding.transport_ops&&p->binding.transport_ops->diagnostics)
            p->binding.transport_ops->diagnostics(p->binding.transport_context,&q->transport);
        memcpy(q->runtime_last_error,p->runtime.last_error,sizeof(q->runtime_last_error));
        ++h->configured_ports;if(q->enabled)++h->enabled_ports;if(q->lifecycle==GATEWAY_PORT_READY)++h->ready_ports;if(q->lifecycle==GATEWAY_PORT_DEGRADED||q->lifecycle==GATEWAY_PORT_ERROR)++h->degraded_or_error_ports;
        add_saturating(&h->active_clients,clients);add_saturating(&h->accepted_transactions,q->transactions.accepted);add_saturating(&h->completed_transactions,q->transactions.completed);
    }
}

unsigned int gateway_port_controller_memory_bytes(void){return(unsigned int)sizeof(gateway_port_controller_t);}
unsigned int gateway_controller_memory_bytes(void){return(unsigned int)sizeof(gateway_controller_t);}
