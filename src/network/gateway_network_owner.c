#include <string.h>
#include "network/gateway_network_owner.h"
#include "network/gateway_dhcp_wire.h"
static int due(core_tick_t now,core_tick_t at){return (int32_t)(now-at)>=0;}
static int equal(const gateway_network_observation_t *a,const gateway_network_observation_t *b)
{
    unsigned int i;
    for(i=0;i<2U;++i)if(a->lan[i].up!=b->lan[i].up||strcmp(a->lan[i].address,b->lan[i].address)||
        strcmp(a->lan[i].netmask,b->lan[i].netmask)||strcmp(a->lan[i].broadcast,b->lan[i].broadcast))return 0;
    return a->default_lan==b->default_lan&&!strcmp(a->gateway,b->gateway)&&
        !strcmp(a->dns[0],b->dns[0])&&!strcmp(a->dns[1],b->dns[1]);
}
int gateway_network_owner_init(gateway_network_owner_t *o,const gateway_network_owner_ops_t *ops,void *context,uint32_t seed)
{
    if(!o||!ops||!ops->observe||!ops->open||!ops->close||!ops->receive||!ops->send||!ops->probe||
       !ops->apply||!ops->poll||!ops->cancel)return -1;
    memset(o,0,sizeof(*o));o->ops=ops;o->context=context;o->next_xid=seed?seed:1;return 0;
}
int gateway_network_owner_policy(gateway_network_owner_t *o,const gateway_network_profile_t *p,core_tick_t now)
{
    unsigned int i;
    if(!o||!p||gateway_network_settings_validate(&p->settings,0))return -1;
    if(o->job&&!o->canceling){if(o->ops->cancel(o->context))return -1;o->canceling=1;}
    for(i=0;i<2U;++i)if(!o->active||p->settings.lan[i].mode!=o->policy.settings.lan[i].mode){
        gateway_dhcp_stop(&o->dhcp[i]);if(o->opened[i])o->ops->close(o->context,i);o->opened[i]=0;
    }
    o->policy=*p;o->active=1;o->stopping=0;o->ready=o->settled=o->error=0;o->dirty=1;++o->generation;
    o->next_observe=o->retry_at=now;return 0;
}
static int target(gateway_network_owner_t *o)
{
    unsigned int i,route=o->policy.settings.default_lan;size_t n;
    o->target=o->observed;o->target.unsupported=0;o->target.default_lan=o->target.default_routes=0;
    memset(o->target.gateway,0,sizeof(o->target.gateway));
    for(i=0;i<2U;++i){gateway_lan_observation_t *lan=&o->target.lan[i];
        if(!lan->present)return -1;
        lan->up=1;
        if(o->policy.settings.lan[i].mode==GATEWAY_LAN_STATIC){
            strcpy(lan->address,o->policy.settings.lan[i].address);strcpy(lan->netmask,o->policy.settings.lan[i].netmask);
            if(gateway_network_profile_broadcast(&o->policy,i,lan->broadcast))return -1;
        }else if(o->dhcp[i].valid){
            strcpy(lan->address,o->dhcp[i].lease.address);strcpy(lan->netmask,o->dhcp[i].lease.netmask);
            strcpy(lan->broadcast,o->dhcp[i].lease.broadcast);
        }else {memset(lan->address,0,16);memset(lan->netmask,0,16);memset(lan->broadcast,0,16);}
    }
    if(route){
        const char *gateway=o->policy.settings.lan[route-1U].mode==GATEWAY_LAN_STATIC?
            o->policy.settings.lan[route-1U].gateway:o->dhcp[route-1U].valid?o->dhcp[route-1U].lease.gateway:"";
        if(gateway[0]){o->target.default_lan=route;o->target.default_routes=1;strcpy(o->target.gateway,gateway);}
    }
    if(o->policy.settings.automatic_dns){
        unsigned int source=o->policy.settings.dns_lan?o->policy.settings.dns_lan:route;
        memset(o->target.dns,0,sizeof(o->target.dns));
        if(source&&o->dhcp[source-1U].valid)memcpy(o->target.dns,o->dhcp[source-1U].lease.dns,sizeof(o->target.dns));
        if(gateway_network_resolver(o->policy.resolver,strlen(o->policy.resolver),(const char (*)[16])o->target.dns,o->resolver,sizeof(o->resolver),&n))return -1;
    }else{memcpy(o->target.dns,o->policy.settings.dns,sizeof(o->target.dns));strcpy(o->resolver,o->policy.resolver);}
    /* Validate the combination of acquired leases with both static policies.
     * Never install overlapping leased networks or a router outside its LAN. */
    {gateway_network_settings_t check;gateway_network_settings_init(&check);
        for(i=0;i<2U;++i){
            if(o->target.lan[i].address[0]){
                strcpy(check.lan[i].address,o->target.lan[i].address);strcpy(check.lan[i].netmask,o->target.lan[i].netmask);
            }else check.lan[i].mode=GATEWAY_LAN_DHCP_CLIENT;
        }
        check.default_lan=o->target.default_lan;
        if(check.default_lan)strcpy(check.lan[check.default_lan-1U].gateway,o->target.gateway);
        memcpy(check.dns,o->target.dns,sizeof(check.dns));if(gateway_network_settings_validate(&check,0))return -1;
    }
    return 0;
}
void gateway_network_owner_step(gateway_network_owner_t *o,core_tick_t now)
{
    unsigned int i,eligible=1;int result;
    if(!o||!o->active)return;
    o->ready=o->settled=0;
    /* Lease validity is a deadline, independent of observation availability. */
    for(i=0;i<2U;++i)if(o->dhcp[i].valid&&due(now,o->dhcp[i].expires_at)){
        gateway_dhcp_step(&o->dhcp[i],now,1);o->dirty=1;
    }
    if(due(now,o->next_observe)){
        if(o->ops->observe(o->context,&o->observed)||o->observed.unsupported){
            o->error=1;
            if(o->job){if(!o->canceling&&!o->ops->cancel(o->context))o->canceling=1;
                if(o->ops->poll(o->context)){o->job=o->canceling=0;o->dirty=1;}}
            return;
        }
        o->next_observe=now+250U;
    }
    for(i=0;i<2U;++i){gateway_dhcp_client_t *d=&o->dhcp[i];
        if(o->policy.settings.lan[i].mode!=GATEWAY_LAN_DHCP_CLIENT)continue;
        if(o->stopping)continue;
        if(!o->opened[i]){
            if(!due(now,o->retry_at)){eligible=0;continue;}
            if(o->ops->open(o->context,i)){o->error=2;o->retry_at=now+1000U;return;}
            o->opened[i]=1;gateway_dhcp_start(d,o->observed.lan[i].mac,++o->next_xid,now);
        }
        gateway_dhcp_step(d,now,o->observed.lan[i].link);
        {unsigned int packet;unsigned char bytes[1514];
            for(packet=0;packet<4U;++packet){
                result=o->ops->receive(o->context,i,bytes,sizeof(bytes));if(!result)break;
                if(result<0){o->error=3;gateway_dhcp_stop(d);o->ops->close(o->context,i);o->opened[i]=0;break;}
                if(gateway_dhcp_arp_conflict(d,bytes,(size_t)result))gateway_dhcp_conflict(d,now);
                else if(gateway_dhcp_frame(d,bytes,(size_t)result,now)>0&&d->state==GATEWAY_DHCP_PROBING)o->probe_at[i]=now+(d->xid%1000U);
            }
        }
        if(d->send_type)(void)o->ops->send(o->context,i,d,now);
        if(d->state==GATEWAY_DHCP_PROBING&&due(now,o->probe_at[i])){
            if(d->probes<3U){
                if(o->ops->probe(o->context,i,d,0)){o->error=4;gateway_dhcp_conflict(d,now);}
                else{++d->probes;o->probe_at[i]=now+(d->probes==3U?2000U:1000U+(d->xid%1000U));}
            }else if(!gateway_dhcp_probed(d,now))(void)o->ops->probe(o->context,i,d,1);
        }
        /* changed includes lease timers, not just effective network values.
         * target/equal below decides whether an address/route/DNS write is
         * necessary. Forcing a job for an unchanged ACK falsely drops ready
         * and aborts WAIT_CONFIRM even though the lease remains valid. Policy,
         * expiry, cancellation and failed writes retain their dirty handling. */
        d->changed=0;
        if(!d->valid)eligible=0;
    }
    if(target(o)){
        o->error=5;eligible=0;
        for(i=0;i<2U;++i)if(o->policy.settings.lan[i].mode==GATEWAY_LAN_DHCP_CLIENT&&
            (o->dhcp[i].valid||o->dhcp[i].state==GATEWAY_DHCP_PROBING))gateway_dhcp_conflict(&o->dhcp[i],now);
        if(target(o))return;
        o->dirty=1;
    }
    if(o->policy.settings.default_lan&&!o->target.default_lan)eligible=0;
    if(o->policy.settings.automatic_dns&&!o->target.dns[0][0])eligible=0;
    if(o->job){
        if((o->job_generation!=o->generation||!equal(&o->writing,&o->target)||due(now,o->job_deadline))&&!o->canceling){
            if(o->ops->cancel(o->context)){o->error=6;return;}o->canceling=1;
        }
        result=o->ops->poll(o->context);if(!result)return;
        o->job=0;o->dirty=o->canceling||o->job_generation!=o->generation||!equal(&o->writing,&o->target);o->next_observe=now;
        if(!o->canceling&&result<0){o->error=7;o->retry_at=now+1000U;return;}
        o->canceling=0;return;
    }
    if((o->dirty||!equal(&o->observed,&o->target))&&due(now,o->retry_at)){
        if(o->ops->apply(o->context,&o->target,o->resolver)){o->error=8;o->retry_at=now+1000U;return;}
        o->writing=o->target;o->job_generation=o->generation;o->job=1;o->dirty=0;o->job_deadline=now+1000U;return;
    }
    if(equal(&o->observed,&o->target)&&!o->dirty){o->error=0;o->ready=eligible;o->settled=1;}
}
void gateway_network_owner_stop(gateway_network_owner_t *o)
{
    unsigned int i;if(!o)return;
    for(i=0;i<2U;++i){gateway_dhcp_stop(&o->dhcp[i]);if(o->opened[i])o->ops->close(o->context,i);o->opened[i]=0;}
    if(o->job&&!o->canceling&&!o->ops->cancel(o->context))o->canceling=1;
    o->ready=o->settled=0;o->stopping=1;o->dirty=1;++o->generation;
}
