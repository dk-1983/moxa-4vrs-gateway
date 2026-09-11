#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "network/gateway_network_system.h"
static unsigned int checks,failed,lan_writes[2],adds,deletes,calls,fail_call,fail_sync;
static gateway_network_observation_t live;
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
int gateway_network_observe(gateway_network_observation_t *out){*out=live;return 0;}
int __real_close(int);
int __real_fsync(int);
int __wrap_fsync(int fd){if(fail_sync){errno=EIO;return -1;}return __real_fsync(fd);}
int __wrap_close(int fd){return fd==1000?0:__real_close(fd);}
int __wrap_socket(int family,int type,int protocol)
{CHECK(family==AF_INET&&type==SOCK_DGRAM&&!protocol);return 1000;}
static void address(struct sockaddr *s,unsigned long ip)
{struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(ip);memcpy(s,&a,sizeof(*s));}
int __wrap_ioctl(int fd,unsigned long command,...)
{
    void *argument;struct ifreq *r;va_list args;unsigned int lan;
    CHECK(fd==1000);va_start(args,command);argument=va_arg(args,void *);va_end(args);
    if(++calls==fail_call){errno=EIO;return -1;}
    if(command==SIOCGIFCONF){struct ifconf *c=argument;memset(c->ifc_req,0,sizeof(struct ifreq));
        strcpy(c->ifc_req[0].ifr_name,"eth2");address(&c->ifc_req[0].ifr_addr,0xc0a8057fUL);
        c->ifc_len=sizeof(struct ifreq);return 0;}
    if(command==SIOCDELRT||command==SIOCADDRT){struct rtentry *route=argument;
        CHECK(!strcmp(route->rt_dev,"eth0")||!strcmp(route->rt_dev,"eth1"));
        CHECK(route->rt_flags==(RTF_UP|RTF_GATEWAY));
        if(command==SIOCDELRT)++deletes;else ++adds;
        return 0;
    }
    r=argument;
    if(command==SIOCGIFNETMASK){CHECK(!strcmp(r->ifr_name,"eth2"));address(&r->ifr_netmask,0xffffff00UL);return 0;}
    CHECK(!strcmp(r->ifr_name,"eth0")||!strcmp(r->ifr_name,"eth1"));lan=!strcmp(r->ifr_name,"eth1");
    if(command==SIOCGIFFLAGS){r->ifr_flags=IFF_UP|IFF_MULTICAST;return 0;}
    CHECK(command==SIOCSIFADDR||command==SIOCSIFNETMASK||command==SIOCSIFBRDADDR||command==SIOCSIFFLAGS);
    if(command==SIOCSIFFLAGS)CHECK(r->ifr_flags==(IFF_UP|IFF_MULTICAST));
    ++lan_writes[lan];return 0;
}
static void reset_counts(void){lan_writes[0]=lan_writes[1]=adds=deletes=calls=fail_call=0;}
int main(void)
{
    gateway_network_observation_t desired;char directory[]="/tmp/4vrs-system-XXXXXX",path[256],tmp[256];FILE *f;unsigned int i,total;
    CHECK(mkdtemp(directory)!=0);snprintf(path,sizeof(path),"%s/resolv",directory);
    snprintf(tmp,sizeof(tmp),"%s/resolv.4vrs.tmp",directory);
    f=fopen(path,"w");CHECK(f!=0);if(!f)return 1;fputs("# exact\nnameserver 10.0.0.1\n",f);fclose(f);
    memset(&live,0,sizeof(live));for(i=0;i<2U;++i)live.lan[i].present=live.lan[i].up=1;
    strcpy(live.lan[0].address,"10.0.2.13");strcpy(live.lan[0].netmask,"255.255.240.0");strcpy(live.lan[0].broadcast,"10.0.2.255");
    strcpy(live.lan[1].address,"192.168.4.127");strcpy(live.lan[1].netmask,"255.255.255.0");strcpy(live.lan[1].broadcast,"192.168.4.255");
    live.default_lan=live.default_routes=1;strcpy(live.gateway,"10.0.0.1");strcpy(live.dns[0],"10.0.0.1");desired=live;
    CHECK(gateway_network_system_write(&desired,path,"# exact\nnameserver 10.0.0.1\n")==0);
    CHECK(!lan_writes[0]&&!lan_writes[1]&&!adds&&!deletes);
    reset_counts();strcpy(desired.gateway,"10.0.0.2");
    CHECK(gateway_network_system_write(&desired,path,"# exact\nnameserver 10.0.0.1\n")==0);
    CHECK(!lan_writes[0]&&!lan_writes[1]&&adds==1&&deletes==1);
    reset_counts();desired=live;strcpy(desired.lan[1].address,"192.168.4.126");
    CHECK(gateway_network_system_write(&desired,path,"# exact\nnameserver 10.0.0.1\n")==0);
    CHECK(!lan_writes[0]&&lan_writes[1]==4&&!adds&&!deletes);total=calls;
    for(i=1;i<=total;++i){reset_counts();fail_call=i;CHECK(gateway_network_system_write(&desired,path,"# exact\nnameserver 10.0.0.1\n")!=0);}
    reset_counts();strcpy(desired.lan[1].netmask,"255.255.254.0");
    CHECK(gateway_network_system_write(&desired,path,"# exact\nnameserver 10.0.0.1\n")!=0);CHECK(!lan_writes[0]&&!lan_writes[1]);
    desired=live;strcpy(desired.gateway,"192.168.4.1");reset_counts();
    CHECK(gateway_network_system_write(&desired,path,"")!=0);CHECK(!calls);
    desired=live;desired.default_lan=0;desired.gateway[0]=0;reset_counts();
    CHECK(gateway_network_system_write(&desired,path,"")==0);CHECK(deletes==1&&!adds);
    fail_sync=1;CHECK(gateway_network_file_replace(path,"new data")!=0);fail_sync=0;
    CHECK(gateway_network_file_replace(path,"confirmed data")==0);
    CHECK(unlink(path)==0);CHECK(symlink("/never/open",path)==0);
    CHECK(gateway_network_file_replace(path,"refused")!=0);CHECK(unlink(path)==0);
    unlink(tmp);CHECK(rmdir(directory)==0);
    printf("network system provider: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
