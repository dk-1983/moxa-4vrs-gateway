#include <string.h>
#include "network/gateway_dhcp_client.h"
static uint32_t get32(const unsigned char *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(unsigned char *p,uint32_t n)
{p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;}
static int due(core_tick_t now,core_tick_t when){return (int32_t)(now-when)>=0;}
static void invalidate(gateway_dhcp_client_t *d)
{if(d->valid)d->changed=1;d->valid=0;d->have_request=0;memset(&d->lease,0,sizeof(d->lease));}
void gateway_dhcp_start(gateway_dhcp_client_t *d,const unsigned char mac[6],uint32_t xid,core_tick_t now)
{memset(d,0,sizeof(*d));memcpy(d->mac,mac,6);d->xid=xid?xid:1;d->state=GATEWAY_DHCP_SELECTING;d->started=now;d->next_send=now;}
void gateway_dhcp_stop(gateway_dhcp_client_t *d)
{invalidate(d);d->state=GATEWAY_DHCP_OFF;d->send_type=0;++d->xid;}
void gateway_dhcp_conflict(gateway_dhcp_client_t *d,core_tick_t now)
{d->valid=0;d->changed=1;d->error=2;d->send_type=4;d->state=GATEWAY_DHCP_BACKOFF;d->next_send=now+10000U;}
void gateway_dhcp_step(gateway_dhcp_client_t *d,core_tick_t now,unsigned int link)
{
    if(d->state==GATEWAY_DHCP_OFF)return;
    if(!link){invalidate(d);d->state=GATEWAY_DHCP_BACKOFF;d->next_send=now+1000U;d->send_type=0;return;}
    if((d->valid||d->state==GATEWAY_DHCP_PROBING)&&due(now,d->expires_at)){
        invalidate(d);d->state=GATEWAY_DHCP_SELECTING;d->started=now;d->next_send=now;d->retries=0;++d->xid;d->error=3;
    }
    if(d->state==GATEWAY_DHCP_BACKOFF){
        if(!due(now,d->next_send))return;
        invalidate(d);d->state=GATEWAY_DHCP_SELECTING;d->started=now;d->retries=0;++d->xid;
    }
    if(d->state==GATEWAY_DHCP_PROBING)return; /* owner performs bounded ARP probes */
    if(d->valid&&due(now,d->rebind_at)&&d->state!=GATEWAY_DHCP_REBINDING){if(d->state==GATEWAY_DHCP_BOUND){++d->xid;d->started=now;d->have_request=0;}d->state=GATEWAY_DHCP_REBINDING;d->next_send=now;d->retries=0;}
    else if(d->state==GATEWAY_DHCP_BOUND&&due(now,d->renew_at)){d->state=GATEWAY_DHCP_RENEWING;d->started=now;d->have_request=0;d->next_send=now;d->retries=0;++d->xid;}
    if(d->state==GATEWAY_DHCP_BOUND)return;
    if(!d->valid&&(uint32_t)(now-d->started)>=30000U){
        d->state=GATEWAY_DHCP_BACKOFF;d->next_send=now+10000U;d->send_type=0;d->error=1;return;
    }
    if(due(now,d->next_send)){
        unsigned int delay=d->retries<4U?(1000U<<d->retries):16000U;
        d->send_type=d->state==GATEWAY_DHCP_SELECTING?1U:3U;
        d->next_send=now+delay+(d->xid&255U);if(d->retries<4U)++d->retries;
    }
}
int gateway_dhcp_receive(gateway_dhcp_client_t *d,const unsigned char *p,size_t n,core_tick_t now)
{
    gateway_dhcp_lease_t lease;gateway_network_settings_t settings;size_t off=240;
    core_tick_t lease_base;
    uint32_t seen=0,server=0,lifetime=0,t1=0,t2=0,ip,mask;unsigned int type=0,end=0;
    if(!d||!p||n<241U||n>1500U||p[0]!=2||p[1]!=1||p[2]!=6||get32(p+4)!=d->xid||
       memcmp(p+28,d->mac,6)||get32(p+236)!=0x63825363UL)return -1;
    if(d->state!=GATEWAY_DHCP_SELECTING&&d->state!=GATEWAY_DHCP_REQUESTING&&
       d->state!=GATEWAY_DHCP_RENEWING&&d->state!=GATEWAY_DHCP_REBINDING)return -1;
    if(d->valid&&due(now,d->expires_at)){gateway_dhcp_step(d,now,1);return -1;}
    memset(&lease,0,sizeof(lease));ip=get32(p+16);
    while(off<n){unsigned int option=p[off++],len,bit=0;const unsigned char *value;
        if(!option)continue;
        if(option==255U){end=1;break;}if(off==n)return -1;
        len=p[off++];if(len>n-off)return -1;value=p+off;off+=len;
        switch(option){
        case 53:bit=1;if(len!=1)return -1;type=value[0];break;
        case 54:bit=2;if(len!=4)return -1;server=get32(value);break;
        case 1:bit=4;if(len!=4)return -1;gateway_ipv4_format(get32(value),lease.netmask);break;
        case 3:bit=8;if(!len||len%4U)return -1;gateway_ipv4_format(get32(value),lease.gateway);break;
        case 6:bit=16;if(!len||len%4U)return -1;gateway_ipv4_format(get32(value),lease.dns[0]);if(len>=8)gateway_ipv4_format(get32(value+4),lease.dns[1]);break;
        case 51:bit=32;if(len!=4)return -1;lifetime=get32(value);break;
        case 58:bit=64;if(len!=4)return -1;t1=get32(value);break;
        case 59:bit=128;if(len!=4)return -1;t2=get32(value);break;
        /* Unowned route options and overload are rejected, not executed or
         * silently applied. The controlled-server plan must use this subset. */
        case 33:case 121:case 249:case 52:return -1;
        default:break;
        }
        if(seen&bit)return -1;
        seen|=bit;
    }
    if(!end||(seen&3U)!=3U||!server||(server>>24)>=224U||(server>>24)==127U)return -1;
    if((d->state==GATEWAY_DHCP_REQUESTING||d->state==GATEWAY_DHCP_RENEWING)&&server!=d->server)return -1;
    if(type==6U&&d->state!=GATEWAY_DHCP_SELECTING){invalidate(d);d->state=GATEWAY_DHCP_BACKOFF;d->next_send=now+1000U;d->send_type=0;d->error=4;return 0;}
    if((d->state==GATEWAY_DHCP_SELECTING&&type!=2U)||(d->state!=GATEWAY_DHCP_SELECTING&&type!=5U))return -1;
    if((seen&36U)!=36U||lifetime<4U||!ip)return -1;
    /* Conservative early expiration bounds all monotonic intervals below 2^31.
     * Infinite/long leases are renewed as a 20-day lease, never used longer. */
    if(lifetime>1728000UL)lifetime=1728000UL;
    if(!t1||t1>=lifetime)t1=lifetime/2U;
    if(!t2||t2>=lifetime)t2=lifetime*7U/8U;
    if(!t1||t1>=t2||t2>=lifetime)return -1;
    /* RFC 2131 4.4.5: delayed replies do not extend the server lease by
     * their transit delay. Keep the earliest request time for this xid,
     * conservatively also across retransmissions and the T2 transition. */
    lease_base=d->have_request?d->requested_at:now;
    if((uint32_t)(now-lease_base)>=lifetime*1000U)return -1;
    gateway_ipv4_format(ip,lease.address);lease.lifetime=lifetime;
    if(d->state==GATEWAY_DHCP_REQUESTING&&strcmp(lease.address,d->lease.address))return -1;
    gateway_network_settings_init(&settings);settings.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;
    strcpy(settings.lan[0].address,lease.address);strcpy(settings.lan[0].netmask,lease.netmask);
    strcpy(settings.lan[0].gateway,lease.gateway);memcpy(settings.dns,lease.dns,sizeof(settings.dns));
    if(gateway_network_settings_validate(&settings,0))return -1;
    {unsigned long parsed;gateway_ipv4_parse(lease.netmask,&parsed);mask=(uint32_t)parsed;}
    gateway_ipv4_format(ip|~mask,lease.broadcast);
    if(d->state==GATEWAY_DHCP_SELECTING){d->lease=lease;d->server=server;d->state=GATEWAY_DHCP_REQUESTING;d->have_request=0;d->send_type=3;d->next_send=now+1000U;d->retries=0;return 0;}
    if(d->valid&&!strcmp(d->lease.address,lease.address)&&!strcmp(d->lease.netmask,lease.netmask))d->state=GATEWAY_DHCP_BOUND;
    else {d->state=GATEWAY_DHCP_PROBING;d->valid=0;d->probes=0;d->next_send=now;}
    d->changed=1;d->lease=lease;d->server=server;d->send_type=0;d->error=0;
    d->renew_at=lease_base+t1*1000U;d->rebind_at=lease_base+t2*1000U;d->expires_at=lease_base+lifetime*1000U;
    return 1;
}
int gateway_dhcp_probed(gateway_dhcp_client_t *d,core_tick_t now)
{
    if(!d||d->state!=GATEWAY_DHCP_PROBING)return -1;
    if(due(now,d->expires_at)){gateway_dhcp_step(d,now,1);return -1;}
    d->state=GATEWAY_DHCP_BOUND;d->valid=1;d->changed=1;return 0;
}
size_t gateway_dhcp_packet(gateway_dhcp_client_t *d,unsigned char *p,size_t cap,core_tick_t now)
{
    unsigned int type=d->send_type;size_t n=240;unsigned long ip=0;unsigned int seconds=(unsigned int)((now-d->started)/1000U);
    if(!type||cap<300U)return 0;
    if(type==3U&&!d->have_request){d->requested_at=now;d->have_request=1;}
    memset(p,0,300);p[0]=1;p[1]=1;p[2]=6;put32(p+4,d->xid);memcpy(p+28,d->mac,6);put32(p+236,0x63825363UL);
    if(seconds>65535U)seconds=65535U;
    p[8]=(unsigned char)(seconds>>8);p[9]=(unsigned char)seconds;
    if(d->valid){gateway_ipv4_parse(d->lease.address,&ip);put32(p+12,(uint32_t)ip);}else p[10]=128;
    p[n++]=53;p[n++]=1;p[n++]=(unsigned char)type;
    p[n++]=61;p[n++]=7;p[n++]=1;memcpy(p+n,d->mac,6);n+=6;
    if(type==3U||type==4U){
        if(!d->valid){gateway_ipv4_parse(d->lease.address,&ip);p[n++]=50;p[n++]=4;put32(p+n,(uint32_t)ip);n+=4;}
        if(d->state==GATEWAY_DHCP_REQUESTING||type==4U){p[n++]=54;p[n++]=4;put32(p+n,d->server);n+=4;}
    }
    p[n++]=55;p[n++]=6;p[n++]=1;p[n++]=3;p[n++]=6;p[n++]=51;p[n++]=58;p[n++]=59;
    p[n++]=255;d->send_type=0;return n<300U?300U:n;
}
