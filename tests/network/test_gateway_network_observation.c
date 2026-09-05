#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include "network/gateway_network_observation.h"
static unsigned int checks,failed;
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL %u %s\n",(unsigned int)__LINE__,#x);}}while(0)
int main(void)
{
    const char lease[]="IPADDR=10.0.2.13\nNETMASK=255.255.240.0\nGATEWAY=10.0.0.1\nDNS=10.0.0.1,10.0.0.3\nLEASETIME=3600\nINTERFACE='eth0'\nHOSTNAME='$(reboot)'\n";
    gateway_dhcp_lease_t l,prior;gateway_network_observation_t o;char b[1024],route[256];size_t i;
    CHECK(gateway_dhcp_lease_decode(lease,strlen(lease),0,&l)==0);
    CHECK(!strcmp(l.broadcast,"10.0.15.255")&&l.lifetime==3600UL);
    CHECK(!strcmp(l.dns[1],"10.0.0.3"));
    prior=l;CHECK(gateway_dhcp_lease_decode(lease,strlen(lease),1,&l)!=0&&!memcmp(&l,&prior,sizeof(l)));
    strcpy(b,lease);strcat(b,"LEASETIME=5\n");CHECK(gateway_dhcp_lease_decode(b,strlen(b),0,&l)!=0);
    strcpy(b,lease);strcat(b,"ROUTE=1.2.3.4,10.0.0.1\n");CHECK(gateway_dhcp_lease_decode(b,strlen(b),0,&l)!=0);
    strcpy(b,lease);strcat(b,"BROADCAST=10.0.2.255\n");CHECK(gateway_dhcp_lease_decode(b,strlen(b),0,&l)!=0);
    strcpy(b,lease);b[4]=0;CHECK(gateway_dhcp_lease_decode(b,strlen(lease),0,&l)!=0);
    for(i=0;i<strlen(lease);++i){memcpy(b,lease,sizeof(lease));b[i]='\0';CHECK(gateway_dhcp_lease_decode(b,strlen(lease),0,&l)!=0);}
    snprintf(route,sizeof(route),"Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT\neth0 00000000 %08lx 0003 0 0 0 00000000 0 0 0\n",(unsigned long)htonl(0x0a000001U));
    memset(&o,0,sizeof(o));CHECK(gateway_network_routes_decode(route,strlen(route),&o)==0);
    CHECK(o.default_lan==1&&o.default_routes==1&&!strcmp(o.gateway,"10.0.0.1"));
    strcpy(b,route);strcat(b,strchr(route,'\n')+1);CHECK(gateway_network_routes_decode(b,strlen(b),&o)!=0);
    printf("network observation checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
