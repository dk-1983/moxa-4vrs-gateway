#include <stdio.h>
#include <string.h>
#include "network/gateway_network_settings.h"
static unsigned int checks,failed;
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL line %u: %s\n",(unsigned int)__LINE__,#x);}}while(0)
static void fixture(gateway_network_settings_t *s)
{
 gateway_network_settings_init(s);s->default_lan=1U;
 strcpy(s->lan[0].address,"10.0.2.13");strcpy(s->lan[0].netmask,"255.255.240.0");strcpy(s->lan[0].gateway,"10.0.0.1");
 strcpy(s->lan[1].address,"192.168.4.127");strcpy(s->lan[1].netmask,"255.255.255.0");strcpy(s->dns[0],"10.0.0.1");
}
int main(void)
{
 gateway_network_settings_t s;unsigned long value;unsigned int i,lan;char full[16];
 const char *bad[]={"","1.2.3","1.2.3.4.5","256.1.1.1","-1.2.3.4","+1.2.3.4","01.2.3.4","1.2.3.4x","1..2.3"};
 CHECK(gateway_ipv4_parse("255.255.255.255",&value)==0&&value==0xffffffffUL);
 for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i)CHECK(gateway_ipv4_parse(bad[i],&value)!=0);
 memset(full,'1',sizeof(full));CHECK(gateway_ipv4_parse(full,&value)!=0);
 fixture(&s);CHECK(s.lan[0].mode==GATEWAY_LAN_STATIC&&s.lan[1].mode==GATEWAY_LAN_STATIC);
 CHECK(gateway_network_settings_validate(&s,&lan)==GATEWAY_NETWORK_SETTINGS_OK);
 strcpy(s.lan[0].netmask,"255.0.255.0");CHECK(gateway_network_settings_validate(&s,&lan)==GATEWAY_NETWORK_SETTINGS_MASK&&lan==0U);
 fixture(&s);strcpy(s.lan[0].address,"10.0.15.255");CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_ADDRESS);
 fixture(&s);strcpy(s.lan[0].gateway,"192.168.4.1");CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_GATEWAY);
 fixture(&s);strcpy(s.lan[1].address,"10.0.3.2");CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_OVERLAP);
 fixture(&s);s.automatic_dns=1U;CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_DNS);
 s.lan[0].mode=GATEWAY_LAN_DHCP_CLIENT;CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_OK);
 s.default_lan=0U;CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_DNS);
 fixture(&s);strcpy(s.dns[0],"224.0.0.1");CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_DNS);
 fixture(&s);strcpy(s.dns[0],"0.0.0.0");CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_DNS);
 fixture(&s);strcpy(s.lan[1].gateway,"0.0.0.0");CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_GATEWAY);
 fixture(&s);s.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;memset(s.lan[1].address,'x',16);CHECK(gateway_network_settings_validate(&s,0)==GATEWAY_NETWORK_SETTINGS_INVALID);
 gateway_network_settings_init(&s);CHECK(gateway_network_settings_validate(&s,0)!=GATEWAY_NETWORK_SETTINGS_OK);
 printf("network settings checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
