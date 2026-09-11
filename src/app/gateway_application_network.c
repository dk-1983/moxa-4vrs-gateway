#include <string.h>
#include "app/gateway_application.h"

int gateway_application_network_boot(const gateway_network_environment_t *environment)
{return gateway_network_boot_restore(environment);}

static unsigned int affinity(const gateway_network_profile_t *p,const gateway_port_config_t *port,unsigned int index)
{
    unsigned int lan=p->affinity[index];
    if(port->bind_policy)return port->bind_policy>1U?port->bind_policy-1U:0U;
    if(!strcmp(port->bind_address,"0.0.0.0"))return 0;
    /* Migration of the first LAN2-only profile: unchanged explicit LAN1 binds
     * gain affinity on use; neither store nor user configuration is rewritten. */
    if(!lan&&!strcmp(p->original_bind[index],p->settings.lan[0].address))lan=1;
    if(lan&&(p->settings.lan[lan-1U].mode==GATEWAY_LAN_DHCP_CLIENT||!strcmp(port->bind_address,p->original_bind[index])||
             !strcmp(port->bind_address,p->settings.lan[lan-1U].address)))return lan;
    return 0;
}
static const char *binding_address(gateway_network_runtime_t *n,const gateway_network_profile_t *p,unsigned int lan)
{
    if(p->settings.lan[lan-1U].mode==GATEWAY_LAN_STATIC)return p->settings.lan[lan-1U].address;
    return n->observed_valid&&!n->observation_error&&n->lease_valid[lan-1U]&&n->observed.lan[lan-1U].address[0]?
        n->observed.lan[lan-1U].address:0;
}
void gateway_application_network_map(gateway_application_t *a,gateway_persistent_config_t *config,int reverse)
{
    unsigned int i;gateway_network_profile_t *p=&a->network.confirmed;
    if(!a->network.available)return;
    for(i=0;i<8U;++i){unsigned int lan=affinity(p,&config->ports[i],i);const char *address;
        if(!lan)continue;
        address=binding_address(&a->network,p,lan);if(!address)continue;
        if(config->ports[i].bind_policy){
            if(!reverse)strcpy(config->ports[i].bind_address,address);
        }else{
            const char *from=reverse?address:p->original_bind[i];
            const char *to=reverse?p->original_bind[i]:address;
            if(!strcmp(config->ports[i].bind_address,from))strcpy(config->ports[i].bind_address,to);
        }
    }
}
static int configuration_busy(gateway_application_t *a)
{
    gateway_configuration_transaction_state_t s=a->configuration_transaction.state;
    return s==GATEWAY_CONFIG_TX_ACTIVATING||s==GATEWAY_CONFIG_TX_PROMOTING||
        s==GATEWAY_CONFIG_TX_ROLLING_BACK||s==GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN;
}
int gateway_application_network_apply(gateway_application_t *a,const gateway_network_settings_t *settings)
{
    gateway_persistent_config_t mapped;gateway_error_t errors[8];unsigned int i;
    if(!a||!settings||a->process_state!=GATEWAY_PROCESS_RUNNING||a->run_control.stop_requested||
       configuration_busy(a)||gateway_network_runtime_busy(&a->network)||!a->network.available||gateway_network_settings_validate(settings,0))return -1;
    mapped=a->coordinator.selected;
    for(i=0;i<8U;++i)if(mapped.ports[i].enabled&&a->coordinator.controller.ports[i].lifecycle!=GATEWAY_PORT_READY&&
        a->coordinator.controller.ports[i].lifecycle!=GATEWAY_PORT_NETWORK_WAIT)return -1;
    for(i=0;i<8U;++i){unsigned int lan=affinity(&a->network.confirmed,&mapped.ports[i],i);
        a->network.binding_affinity[i]=lan;
        if(lan&&settings->lan[lan-1U].mode==GATEWAY_LAN_STATIC)strcpy(mapped.ports[i].bind_address,settings->lan[lan-1U].address);
    }
    if(gateway_configuration_validate(mapped.ports,errors))return -1;
    {int r=gateway_network_runtime_start(&a->network,settings);if(!r){gateway_application_revision_advance(a);a->network_operation=a->revision;}return r;}
}
static void dynamic_bindings(gateway_application_t *a,core_tick_t now)
{
    gateway_network_runtime_t *n=&a->network;unsigned int i,index;
    for(i=0;i<2U;++i)if(n->lease_valid[i]&&(int32_t)(now-n->lease_expiry[i])>=0)n->lease_valid[i]=0;
    for(i=0;i<8U;++i){unsigned int lan=affinity(&n->confirmed,&a->coordinator.selected.ports[i],i);
        if(lan&&n->confirmed.settings.lan[lan-1U].mode==GATEWAY_LAN_DHCP_CLIENT)
            (void)gateway_controller_network_wait(&a->coordinator.controller,i,binding_address(n,&n->confirmed,lan)?0U:1U,now);
    }
    index=n->dynamic_index;n->dynamic_index=(index+1U)%8U;
    {gateway_port_config_t desired=a->coordinator.selected.ports[index];
        gateway_port_controller_t *port=&a->coordinator.controller.ports[index];
        unsigned int lan=affinity(&n->confirmed,&desired,index);const char *address;
        if(!lan||n->confirmed.settings.lan[lan-1U].mode!=GATEWAY_LAN_DHCP_CLIENT)return;
        address=binding_address(n,&n->confirmed,lan);if(!address)return;
        strcpy(desired.bind_address,address);
        if(!desired.enabled){strcpy(a->coordinator.selected.ports[index].bind_address,address);return;}
        if(port->lifecycle==GATEWAY_PORT_READY&&!strcmp(port->current.bind_address,address))
            strcpy(a->coordinator.selected.ports[index].bind_address,address);
        else if(port->lifecycle==GATEWAY_PORT_READY||port->lifecycle==GATEWAY_PORT_NETWORK_WAIT)
            (void)gateway_controller_reconfigure(&a->coordinator.controller,index,&desired,now);
    }
}
int gateway_application_network_keep(gateway_application_t *a)
{
    if(!a||a->network.status.state!=GATEWAY_NETWORK_WAIT_CONFIRM||!a->network.binding_ack)return -1;
    return gateway_network_runtime_command(&a->network,'K');
}
int gateway_application_network_revert(gateway_application_t *a)
{return a?gateway_network_runtime_command(&a->network,'R'):-1;}
void gateway_application_network_step(gateway_application_t *a,core_tick_t now)
{
    gateway_network_runtime_t *n=&a->network;gateway_port_controller_t *port;
    gateway_port_config_t desired;gateway_network_profile_t *profile;unsigned int index;
    if(a->run_control.stop_requested)gateway_network_runtime_stop(n);
    gateway_network_runtime_poll(n);
    if(a->revision_network_state!=(unsigned int)n->status.state){a->revision_network_state=(unsigned int)n->status.state;gateway_application_revision_advance(a);}
    if(a->run_control.stop_requested||!a->coordinator.controller_initialized)return;
    if(n->available&&!gateway_network_runtime_busy(n)&&!configuration_busy(a))dynamic_bindings(a,now);
    if(n->status.state==GATEWAY_NETWORK_KEPT){
        for(index=0;index<8U;++index)if(n->binding_affinity[index]){
            const char *address=binding_address(n,&n->confirmed,n->binding_affinity[index]);
            if(address)strcpy(a->coordinator.selected.ports[index].bind_address,address);
            n->binding_affinity[index]=0;
        }
    }
    if(n->status.state==GATEWAY_NETWORK_WAIT_CONFIRM){
        for(index=0;index<8U;++index)if(a->coordinator.selected.ports[index].enabled&&
            a->coordinator.controller.ports[index].lifecycle!=GATEWAY_PORT_READY)
                (void)gateway_application_network_revert(a);
    }
    if(n->status.state!=GATEWAY_NETWORK_WAIT_BINDINGS&&n->status.state!=GATEWAY_NETWORK_ROLLBACK_BINDINGS)return;
    if(n->binding_ack)return;
    profile=n->status.state==GATEWAY_NETWORK_WAIT_BINDINGS?&n->candidate:&n->confirmed;
    if(n->environment&&n->environment->service&&!n->observed_valid)return;
    if(n->binding_index==8U){
        if(n->status.state==GATEWAY_NETWORK_WAIT_BINDINGS){
            for(index=0;index<8U;++index)if(a->coordinator.selected.ports[index].enabled&&
                a->coordinator.controller.ports[index].lifecycle!=GATEWAY_PORT_READY){
                    (void)gateway_application_network_revert(a);return;
                }
        }
        if(n->recovering){n->recovering=0;n->binding_ack=1;n->status.state=n->recovery_kept?GATEWAY_NETWORK_KEPT:GATEWAY_NETWORK_REVERTED;}
        else if(!gateway_network_runtime_command(n,'B'))n->binding_ack=1;
        return;
    }
    index=n->binding_index;port=&a->coordinator.controller.ports[index];desired=a->coordinator.selected.ports[index];
    if(!n->binding_affinity[index]){++n->binding_index;return;}
    {const char *address=binding_address(n,profile,n->binding_affinity[index]);
        if(!address){
            if(n->status.state==GATEWAY_NETWORK_ROLLBACK_BINDINGS){
                if(gateway_controller_network_wait(&a->coordinator.controller,index,1,now))return;
                ++n->binding_index;n->binding_wait=0;
            }
            return;
        }
        strcpy(desired.bind_address,address);
    }
    (void)gateway_controller_network_wait(&a->coordinator.controller,index,0,now);
    if(!memcmp(&port->current,&desired,sizeof(desired))&&
       port->lifecycle==(desired.enabled?GATEWAY_PORT_READY:GATEWAY_PORT_DISABLED)){
        ++n->binding_index;n->binding_wait=0;return;
    }
    if(!n->binding_wait){
        if(gateway_controller_reconfigure(&a->coordinator.controller,index,&desired,now)){
            (void)gateway_application_network_revert(a);return;
        }
        n->binding_wait=1;return;
    }
    if(port->lifecycle==GATEWAY_PORT_ERROR||port->lifecycle==GATEWAY_PORT_DEGRADED||
       port->lifecycle==GATEWAY_PORT_READY||port->lifecycle==GATEWAY_PORT_DISABLED)
        (void)gateway_application_network_revert(a);
}
