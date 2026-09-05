#include <stdio.h>
#include <string.h>
#include "network/gateway_network_settings.h"

void gateway_network_settings_init(gateway_network_settings_t *s)
{if(s)memset(s,0,sizeof(*s));}

int gateway_ipv4_parse(const char text[GATEWAY_IPV4_TEXT_MAX],unsigned long *value)
{
    unsigned int part,pos=0;unsigned long result=0;
    if(!text||!value)return -1;
    for(part=0;part<4U;++part){
        unsigned int n=0,digits=0,start=pos;
        while(pos<GATEWAY_IPV4_TEXT_MAX && text[pos]>='0' && text[pos]<='9'){
            n=n*10U+(unsigned int)(text[pos]-'0');++pos;++digits;
            if(digits>3U||n>255U)return -1;
        }
        if(!digits||(digits>1U&&text[start]=='0')||pos>=GATEWAY_IPV4_TEXT_MAX)return -1;
        result=(result<<8)|n;
        if(part<3U){if(text[pos++]!='.')return -1;}
        else if(text[pos]!='\0')return -1;
    }
    *value=result;return 0;
}
static int unicast(unsigned long a)
{unsigned long first=a>>24;return first!=0UL&&first!=127UL&&first<224UL;}
static int optional_address(const char *s,unsigned long *a)
{if(s[0]=='\0'){*a=0;return 0;}if(gateway_ipv4_parse(s,a))return -1;return *a?0:-1;}

gateway_network_settings_result_t gateway_network_settings_validate(
    const gateway_network_settings_t *s,unsigned int *invalid_lan)
{
    unsigned int i;unsigned long addr[2]={0,0},mask[2]={0,0},gw[2]={0,0};
    if(invalid_lan)*invalid_lan=GATEWAY_LAN_COUNT;
    if(!s||s->default_lan>GATEWAY_LAN_COUNT||s->automatic_dns>1U||s->dns_lan>2U||
       (!s->automatic_dns&&s->dns_lan))return GATEWAY_NETWORK_SETTINGS_INVALID;
    for(i=0;i<GATEWAY_LAN_COUNT;++i){
        const gateway_lan_settings_t *p=&s->lan[i];unsigned long inverse;
        if(invalid_lan)*invalid_lan=i;
        if(p->mode!=GATEWAY_LAN_STATIC&&p->mode!=GATEWAY_LAN_DHCP_CLIENT)return GATEWAY_NETWORK_SETTINGS_INVALID;
        if(!memchr(p->address,0,sizeof(p->address))||!memchr(p->netmask,0,sizeof(p->netmask))||
            !memchr(p->gateway,0,sizeof(p->gateway)))return GATEWAY_NETWORK_SETTINGS_INVALID;
        /* Static fallback fields remain dormant while DHCP is selected. */
        if(p->mode==GATEWAY_LAN_DHCP_CLIENT){unsigned long dormant;
            if((p->address[0]&&gateway_ipv4_parse(p->address,&dormant))||
                (p->netmask[0]&&gateway_ipv4_parse(p->netmask,&dormant))||
                (p->gateway[0]&&gateway_ipv4_parse(p->gateway,&dormant)))return GATEWAY_NETWORK_SETTINGS_INVALID;
            continue;
        }
        if(gateway_ipv4_parse(p->address,&addr[i])||!unicast(addr[i]))return GATEWAY_NETWORK_SETTINGS_ADDRESS;
        if(gateway_ipv4_parse(p->netmask,&mask[i]))return GATEWAY_NETWORK_SETTINGS_MASK;
        inverse=(~mask[i])&0xffffffffUL;
        /* First-release Ethernet policy: contiguous /1 through /30 masks. */
        if(!mask[i]||inverse<3UL||(inverse&(inverse+1UL)))return GATEWAY_NETWORK_SETTINGS_MASK;
        if((addr[i]&inverse)==0UL||(addr[i]&inverse)==inverse)return GATEWAY_NETWORK_SETTINGS_ADDRESS;
        if(optional_address(p->gateway,&gw[i]))return GATEWAY_NETWORK_SETTINGS_GATEWAY;
        if(gw[i] && (!unicast(gw[i])||gw[i]==addr[i]||(gw[i]&mask[i])!=(addr[i]&mask[i])||
            !(gw[i]&inverse)||(gw[i]&inverse)==inverse))return GATEWAY_NETWORK_SETTINGS_GATEWAY;
        if(s->default_lan==i+1U&&!gw[i])return GATEWAY_NETWORK_SETTINGS_GATEWAY;
    }
    if(invalid_lan)*invalid_lan=GATEWAY_LAN_COUNT;
    if(s->lan[0].mode==GATEWAY_LAN_STATIC&&s->lan[1].mode==GATEWAY_LAN_STATIC){
        unsigned long common=mask[0]&mask[1];
        if((addr[0]&common)==(addr[1]&common))return GATEWAY_NETWORK_SETTINGS_OVERLAP;
    }
    if(s->automatic_dns){
        for(i=0;i<2U;++i){unsigned long dormant;
            if(!memchr(s->dns[i],0,sizeof(s->dns[i]))||
               (s->dns[i][0]&&gateway_ipv4_parse(s->dns[i],&dormant)))return GATEWAY_NETWORK_SETTINGS_INVALID;
        }
        {unsigned int source=s->dns_lan?s->dns_lan:s->default_lan;
            if(!source||s->lan[source-1U].mode!=GATEWAY_LAN_DHCP_CLIENT)return GATEWAY_NETWORK_SETTINGS_DNS;}
    } else for(i=0;i<2U;++i){unsigned long a;if(optional_address(s->dns[i],&a)||(a&&!unicast(a)))return GATEWAY_NETWORK_SETTINGS_DNS;}
    return GATEWAY_NETWORK_SETTINGS_OK;
}

void gateway_ipv4_format(unsigned long a,char b[16])
{snprintf(b,16,"%lu.%lu.%lu.%lu",(a>>24)&255UL,(a>>16)&255UL,(a>>8)&255UL,a&255UL);}
