#define _GNU_SOURCE
#include <net/if.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include "network/gateway_network_runtime.h"
static unsigned int checks,failed,writes,calls,fail_call,closes;
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
int __wrap_socket(int family,int type,int protocol)
{CHECK(family==AF_INET&&type==SOCK_DGRAM&&protocol==0);return 42;}
int __wrap_close(int fd){CHECK(fd==42);++closes;return 0;}
static void address(struct sockaddr *s,unsigned long ip)
{struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(ip);memcpy(s,&a,sizeof(*s));}
int __wrap_ioctl(int fd,unsigned long command,...)
{
    void *argument;struct ifreq *r;va_list args;CHECK(fd==42);
    va_start(args,command);argument=va_arg(args,void *);va_end(args);
    if(++calls==fail_call)return -1;
    if(command==SIOCGIFCONF){struct ifconf *c=argument;memset(c->ifc_req,0,3U*sizeof(struct ifreq));
        strcpy(c->ifc_req[0].ifr_name,"eth0");address(&c->ifc_req[0].ifr_addr,0x0a00020dUL);
        strcpy(c->ifc_req[1].ifr_name,"eth2");address(&c->ifc_req[1].ifr_addr,0xc0a8057fUL);
        strcpy(c->ifc_req[2].ifr_name,"eth1");address(&c->ifc_req[2].ifr_addr,0xc0a8047fUL);
        c->ifc_len=3U*sizeof(struct ifreq);return 0;}
    r=argument;
    if(command==SIOCGIFNETMASK){address(&r->ifr_netmask,!strcmp(r->ifr_name,"eth0")?0xfffff000UL:0xffffff00UL);return 0;}
    CHECK(!strcmp(r->ifr_name,"eth1"));
    if(command==SIOCGIFFLAGS){r->ifr_flags=IFF_UP|IFF_MULTICAST;return 0;}
    CHECK(command==SIOCSIFADDR||command==SIOCSIFNETMASK||command==SIOCSIFBRDADDR||command==SIOCSIFFLAGS);
    if(command==SIOCSIFFLAGS)CHECK(r->ifr_flags==(IFF_UP|IFF_MULTICAST));
    ++writes;return 0;
}
int main(void)
{
    gateway_lan_observation_t lan;const gateway_network_environment_t *e=gateway_network_environment_production();unsigned int i;
    memset(&lan,0,sizeof(lan));lan.present=lan.up=1;strcpy(lan.address,"192.168.4.126");
    strcpy(lan.netmask,"255.255.255.0");strcpy(lan.broadcast,"192.168.4.255");
    CHECK(e->write_lan2(0,&lan)==0);CHECK(writes==4&&closes==1);
    writes=calls=0;strcpy(lan.netmask,"255.255.254.0");CHECK(e->write_lan2(0,&lan)!=0);CHECK(writes==0); /* vendor eth2 overlap */
    strcpy(lan.netmask,"255.255.255.0");strcpy(lan.address,"10.0.2.14");writes=calls=0;
    CHECK(e->write_lan2(0,&lan)!=0);CHECK(writes==0); /* LAN1 overlap */
    strcpy(lan.address,"192.168.4.126");
    for(i=1;i<=8U;++i){fail_call=i;calls=writes=0;CHECK(e->write_lan2(0,&lan)!=0);}
    printf("active LAN2 provider: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
