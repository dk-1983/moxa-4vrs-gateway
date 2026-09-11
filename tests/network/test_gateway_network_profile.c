#include <stdio.h>
#include <string.h>
#include "network/gateway_network_profile.h"
static unsigned int checks,failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
static gateway_network_profile_t p,c,q;
static char bytes[GATEWAY_NETWORK_SNAPSHOT_MAX],bad[GATEWAY_NETWORK_SNAPSHOT_MAX];
int main(void)
{
    size_t n,i;gateway_network_settings_t settings;
    strcpy(p.interfaces,"auto lo eth0 eth1 eth2\niface lo inet loopback\niface eth0 inet static\n address 10.0.2.13\n netmask 255.255.240.0\n gateway 10.0.0.1\niface eth1 inet static\n address 192.168.4.127\n netmask 255.255.255.0\niface eth2 inet static\n address 192.168.5.127\n netmask 255.255.255.0\n");
    strcpy(p.resolver,"# preserved\nnameserver 10.0.0.1\n");
    CHECK(gateway_network_import(p.interfaces,strlen(p.interfaces),p.resolver,strlen(p.resolver),&p.settings)==0);
    for(i=0;i<8U;++i)strcpy(p.original_bind[i],i?"0.0.0.0":"10.0.2.13");
    p.affinity[0]=1;
    CHECK(gateway_network_profile_encode(&p,bytes,sizeof(bytes),&n)==0);
    CHECK(gateway_network_profile_decode(bytes,n,&q)==0);
    CHECK(!memcmp(&p.settings,&q.settings,sizeof(p.settings)));
    CHECK(!strcmp(q.interfaces,p.interfaces)&&!strcmp(q.resolver,p.resolver));
    CHECK(q.affinity[0]==1U&&!strcmp(q.original_bind[0],"10.0.2.13"));
    for(i=0;i<n;++i)CHECK(gateway_network_profile_decode(bytes,i,&q)!=0);
    memcpy(bad,bytes,n);bad[15]='-';CHECK(gateway_network_profile_decode(bad,n,&q)!=0);
    settings=p.settings;settings.lan[0].mode=GATEWAY_LAN_DHCP_CLIENT;settings.automatic_dns=1;
    CHECK(gateway_network_profile_candidate(&p,&settings,&c)==0);
    CHECK(strstr(c.interfaces,"iface eth0 inet dhcp\n")!=0);
    CHECK(!strstr(c.resolver,"nameserver"));
    CHECK(gateway_network_profile_encode(&c,bytes,sizeof(bytes),&n)==0);
    CHECK(gateway_network_profile_decode(bytes,n,&q)==0);
    CHECK(q.settings.default_lan==1U&&q.settings.automatic_dns==1U);
    CHECK(!strcmp(q.settings.lan[0].address,"10.0.2.13")); /* dormant fallback survives */
    CHECK(!strcmp(q.settings.dns[0],"10.0.0.1"));
    CHECK(q.affinity[0]==1U);
    settings.default_lan=0;settings.dns_lan=1;
    CHECK(gateway_network_profile_candidate(&p,&settings,&c)==0);
    CHECK(gateway_network_profile_encode(&c,bytes,sizeof(bytes),&n)==0);
    CHECK(!strncmp(bytes,"4VRS_PROFILE_2 ",15));
    CHECK(gateway_network_profile_decode(bytes,n,&q)==0);
    CHECK(q.settings.default_lan==0&&q.settings.automatic_dns==1&&q.settings.dns_lan==1);
    CHECK(gateway_network_profile_encode(&p,bytes,sizeof(bytes),&n)==0);
    CHECK(!strncmp(bytes,"4VRS_PROFILE_1 ",15)); /* no forced rewrite of an enrolled profile */
    strcpy(c.settings.lan[1].address,"192.168.4.126");
    CHECK(gateway_network_profile_encode(&c,bytes,sizeof(bytes),&n)!=0); /* metadata/document mismatch */
    printf("network profile checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
