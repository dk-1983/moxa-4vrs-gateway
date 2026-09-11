#include <stdio.h>
#include <string.h>
#include "network/gateway_network_document.h"

static unsigned int checks,failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
static const char baseline[] =
    "# retained\nauto eth0 eth1 eth2 lo\niface lo inet loopback\n"
    "iface eth0 inet static\n address 10.0.2.13\n netmask 255.255.240.0\n"
    " broadcast 10.0.2.255\n gateway 10.0.0.1\n dns-nameserver 10.0.0.1 10.0.0.3\n"
    " up /vendor/hook eth0\n# LAN2\niface eth1 inet static\n"
    " address 192.168.4.127\n netmask 255.255.255.0\n"
    "iface eth2 inet static\n address 192.168.5.127\n netmask 255.255.255.0\n";
static const char resolver[]="search example.test\nnameserver 10.0.0.1\n# DNS\nnameserver 10.0.0.3\n";
int main(void)
{
    gateway_network_settings_t s,t,unchanged;
    char output[4096],input[4096]; size_t n;
    CHECK(gateway_network_import(baseline,strlen(baseline),resolver,strlen(resolver),&s)==0);
    CHECK(!strcmp(s.lan[0].address,"10.0.2.13") && s.default_lan==1U && !s.automatic_dns);
    CHECK(!strcmp(s.dns[1],"10.0.0.3"));
    {
        gateway_network_settings_t route=s;
        strcpy(route.lan[0].gateway,"10.0.0.2");
        CHECK(gateway_network_render(baseline,strlen(baseline),&route,output,sizeof(output),&n)==0);
        CHECK(strstr(output," broadcast 10.0.2.255\n")!=0);
        CHECK(strstr(output," address 10.0.2.13\n netmask 255.255.240.0\n")!=0);
        CHECK(strstr(output,"gateway 10.0.0.1")==0);
        CHECK(strstr(output,"gateway 10.0.0.2")!=0);
        CHECK(gateway_network_import(output,n,resolver,strlen(resolver),&t)==0);
        CHECK(!memcmp(&route,&t,sizeof(route)));
        route.default_lan=0;route.lan[0].gateway[0]=0;
        CHECK(gateway_network_render(baseline,strlen(baseline),&route,output,sizeof(output),&n)==0);
        CHECK(strstr(output,"gateway ")==0);
        CHECK(strstr(output," broadcast 10.0.2.255\n")!=0);
        CHECK(strstr(output," up /vendor/hook eth0\n# LAN2\n")!=0);
        CHECK(!strcmp(strstr(output,"iface eth1"),strstr(baseline,"iface eth1")));
    }
    strcpy(s.lan[1].address,"192.168.4.126");
    CHECK(gateway_network_render(baseline,strlen(baseline),&s,output,sizeof(output),&n)==0);
    CHECK(n==strlen(output));
    CHECK(strstr(output," up /vendor/hook eth0\n# LAN2\n")!=0);
    CHECK(strstr(output,"iface eth2 inet static\n address 192.168.5.127\n netmask 255.255.255.0\n")!=0);
    CHECK(strstr(output,"iface lo inet loopback\n")!=0);
    CHECK(strstr(output," broadcast 10.0.2.255\n")!=0);
    CHECK(strstr(output,"address 192.168.4.126\n")!=0);
    CHECK(gateway_network_import(output,n,resolver,strlen(resolver),&t)==0);
    CHECK(!memcmp(&s,&t,sizeof(s)));
    s.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;
    CHECK(gateway_network_render(baseline,strlen(baseline),&s,output,sizeof(output),&n)==0);
    CHECK(strstr(output,"iface eth1 inet dhcp\niface eth2")!=0);
    CHECK(!strstr(output,"address 192.168.4."));
    CHECK(gateway_network_render(baseline,strlen(baseline),&s,output,8,&n)!=0 && !n);
    strcpy(input,baseline); strcat(input,"iface eth0 inet static\n");
    memset(&t,0x5a,sizeof(t)); unchanged=t;
    CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&t)!=0);
    CHECK(!memcmp(&t,&unchanged,sizeof(t)));
    strcpy(input,baseline); strcat(input,"source /etc/network/more\n");
    CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&t)!=0);
    strcpy(input,"iface eth0 inet static\n address 10.0.2.13\n netmask 255.255.240.0\n metric 100\niface eth1 inet static\n address 192.168.4.127\n netmask 255.255.255.0\n");
    CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&t)!=0);
    strcpy(input,baseline); input[10]='\0';
    CHECK(gateway_network_import(input,strlen(baseline),resolver,strlen(resolver),&t)!=0);
    strcpy(input,baseline); strcat(input,"iface eth0 inet6 static\n");
    CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&t)!=0);
    strcpy(input,resolver); strcat(input,"nameserver 1.1.1.1\n");
    CHECK(gateway_network_import(baseline,strlen(baseline),input,strlen(input),&t)!=0);
    CHECK(gateway_network_resolver(resolver,strlen(resolver),s.dns,output,sizeof(output),&n)==0);
    CHECK(strstr(output,"search example.test\n# DNS\n")!=0);
    CHECK(strstr(output,"nameserver 10.0.0.1\nnameserver 10.0.0.3\n")!=0);
    CHECK(gateway_network_resolver(resolver,strlen(resolver),s.dns,output,10,&n)!=0 && !n);
    {const char empty[2][GATEWAY_IPV4_TEXT_MAX]={{0},{0}};
        strcpy(output,"stale DNS");
        CHECK(gateway_network_resolver("nameserver 10.0.0.1\n",20,empty,output,sizeof(output),&n)==0 && !n && !output[0]);
    }
    strcpy(s.dns[0],"1.2.3.4;reboot");
    CHECK(gateway_network_resolver(resolver,strlen(resolver),s.dns,output,sizeof(output),&n)!=0 && !n);
    {const char *lan2=strstr(baseline,"iface eth1 inet static\n");size_t prefix=(size_t)(lan2-baseline)+strlen("iface eth1 inet static\n");
        snprintf(input,sizeof(input),"%.*s\tnetwork 192.168.4.0\n%s",(int)prefix,baseline,baseline+prefix);
        CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&s)==0);
        CHECK(gateway_network_render(input,strlen(input),&s,output,sizeof(output),&n)==0);
        CHECK(n==strlen(input)&&!memcmp(input,output,n));
        strcpy(s.lan[1].address,"192.168.6.126");strcpy(s.lan[1].netmask,"255.255.254.0");
        CHECK(gateway_network_render(input,strlen(input),&s,output,sizeof(output),&n)==0);
        CHECK(strstr(output,"network 192.168.6.0\n")!=0);
        CHECK(strstr(output,"broadcast 192.168.7.255\n")!=0);
        CHECK(gateway_network_import(output,n,resolver,strlen(resolver),&t)==0);
        CHECK(!memcmp(&s,&t,sizeof(s)));
    }
    {
        const char *bad[]={"", "10.0.0.1 trailing", "10.0.0.1;true", "0.0.0.0",
            "127.0.0.1", "224.0.0.1", "10.0.0.1 10.0.0.1", "10.0.0.1 10.0.0.3 10.0.0.4",
            "10.0.0.9", "10.0.0.3 10.0.0.1", "10.0.0.1\n dns-nameservers 10.0.0.3"};
        unsigned int i;const char *at=strstr(baseline," dns-nameserver"),*tail=strchr(at,'\n');
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            snprintf(input,sizeof(input),"%.*s dns-nameserver %s%s",(int)(at-baseline),baseline,bad[i],tail);
            CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&t)!=0);
        }
        CHECK(gateway_network_import(baseline,strlen(baseline),resolver,strlen(resolver),&s)==0);
        strcpy(s.lan[0].address,"10.0.2.14");
        CHECK(gateway_network_render(baseline,strlen(baseline),&s,output,sizeof(output),&n)==0);
        CHECK(strstr(output," dns-nameserver 10.0.0.1 10.0.0.3\n")!=0);
        strcpy(s.dns[0],"10.0.0.9");s.dns[1][0]=0;
        CHECK(gateway_network_render(baseline,strlen(baseline),&s,output,sizeof(output),&n)==0);
        CHECK(strstr(output,"dns-nameserver 10.0.0.9\n")!=0);
        CHECK(gateway_network_import(output,n,"nameserver 10.0.0.9\n",20,&t)==0);
        s.automatic_dns=1;s.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;s.dns_lan=2;
        CHECK(gateway_network_render(baseline,strlen(baseline),&s,output,sizeof(output),&n)==0);
        CHECK(strstr(output,"dns-nameserver")==0);
        CHECK(gateway_network_import(output,n,"",0,&t)==0);
    }
    {
        const char *at=strstr(baseline," dns-nameserver"),*tail=strchr(at,'\n');
        const char *bad[]={"10.0.0.9", "10.0.0.1 10.0.0.1", "999.0.0.1",
            "10.0.0.1\n dns-nameservers 10.0.0.3"};unsigned int i;
        snprintf(input,sizeof(input),"%.*s dns-servers 10.0.0.1 10.0.0.3%s",(int)(at-baseline),baseline,tail);
        CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&t)!=0);
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
            snprintf(input,sizeof(input),"%.*s dns-nameservers %s%s",(int)(at-baseline),baseline,bad[i],tail);
            CHECK(gateway_network_import(input,strlen(input),resolver,strlen(resolver),&t)!=0);
        }
        snprintf(input,sizeof(input),"%.*s dns-nameservers 10.0.0.1 10.0.0.3%s",(int)(at-baseline),baseline,tail);
        CHECK(!gateway_network_import(input,strlen(input),resolver,strlen(resolver),&s));
        CHECK(!gateway_network_render(input,strlen(input),&s,output,sizeof(output),&n));
        CHECK(n==strlen(input)&&!memcmp(input,output,n));
        strcpy(s.dns[0],"10.0.0.9");s.dns[1][0]=0;
        CHECK(!gateway_network_render(input,strlen(input),&s,output,sizeof(output),&n));
        CHECK(strstr(output,"dns-nameservers 10.0.0.9\n")!=0);
        s.automatic_dns=1;s.dns_lan=2;s.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;
        CHECK(!gateway_network_render(input,strlen(input),&s,output,sizeof(output),&n));
        CHECK(strstr(output,"dns-nameservers")==0);
    }
    printf("network document checks=%u failed=%u\n",checks,failed);
    return failed?1:0;
}
