#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "network/gateway_network_observation.h"
static unsigned int checks,failed,closed,ioctls,opened;
static int fail_ioctl,fail_read,oversize;
static size_t offset[2];static char route[256];
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL %u %s\n",(unsigned int)__LINE__,#x);}}while(0)
int __wrap_socket(int domain,int type,int protocol)
{CHECK(domain==AF_INET&&type==SOCK_DGRAM&&protocol==0);return 100;}
int __wrap_open(const char *path,int flags,...)
{CHECK(flags==O_RDONLY);++opened;if(!strcmp(path,"/proc/net/route"))return 101;if(!strcmp(path,"/etc/resolv.conf"))return 102;CHECK(0);return -1;}
int __wrap_close(int fd){CHECK(fd>=100&&fd<=102);++closed;return 0;}
ssize_t __wrap_read(int fd,void *b,size_t cap)
{const char *s;size_t n;CHECK(fd==101||fd==102);if(fail_read){errno=EIO;return -1;}if(oversize){memset(b,'x',cap);return (ssize_t)cap;}s=fd==101?route:"# retained\nsearch example.test\nnameserver 10.0.0.1\nnameserver 10.0.0.3\n";n=strlen(s)-offset[fd-101];if(n>cap)n=cap;memcpy(b,s+offset[fd-101],n);offset[fd-101]+=n;return (ssize_t)n;}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    va_list args;struct ifreq *r;unsigned int lan;struct sockaddr_in *a;
    CHECK(fd==100);++ioctls;if(fail_ioctl){errno=EIO;return -1;}
    va_start(args,request);r=va_arg(args,struct ifreq*);va_end(args);
    CHECK(!strcmp(r->ifr_name,"eth0")||!strcmp(r->ifr_name,"eth1"));lan=(unsigned int)(r->ifr_name[3]-'0');
    if(request==SIOCGIFFLAGS){r->ifr_flags=IFF_UP|(lan?0:IFF_RUNNING);return 0;}
    if(request==SIOCGIFHWADDR){memset(r->ifr_hwaddr.sa_data,0,6);r->ifr_hwaddr.sa_data[5]=(char)(0xf1U+lan);return 0;}
    a=(struct sockaddr_in*)&r->ifr_addr;a->sin_family=AF_INET;
    if(request==SIOCGIFADDR)a->sin_addr.s_addr=htonl(lan?0xc0a8047fU:0x0a00020dU);
    else if(request==SIOCGIFNETMASK)a->sin_addr.s_addr=htonl(lan?0xffffff00U:0xfffff000U);
    else if(request==SIOCGIFBRDADDR)a->sin_addr.s_addr=htonl(lan?0xc0a804ffU:0x0a0002ffU);
    else{CHECK(0);return -1;} /* Any mutating ioctl is a test failure. */
    return 0;
}
static void reset(void){closed=ioctls=opened=0;memset(offset,0,sizeof(offset));fail_ioctl=fail_read=oversize=0;}
int main(void)
{
    gateway_network_observation_t observed,old;
    snprintf(route,sizeof(route),"Iface Destination Gateway Flags RefCnt Use Metric Mask\neth0 00000000 %08lx 0003 0 0 0 00000000\n",(unsigned long)htonl(0x0a000001U));
    reset();CHECK(gateway_network_observe(&observed)==0);CHECK(closed==3&&opened==2&&ioctls==10);
    CHECK(observed.lan[0].up&&observed.lan[0].link&&observed.lan[1].up&&!observed.lan[1].link);
    CHECK(!strcmp(observed.lan[0].broadcast,"10.0.2.255")&&!strcmp(observed.lan[1].address,"192.168.4.127"));
    CHECK(observed.default_lan==1&&!strcmp(observed.dns[1],"10.0.0.3"));old=observed;
    reset();fail_ioctl=1;CHECK(gateway_network_observe(&observed)!=0&&closed==1&&!memcmp(&observed,&old,sizeof(old)));
    reset();fail_read=1;CHECK(gateway_network_observe(&observed)!=0&&closed==2&&!memcmp(&observed,&old,sizeof(old)));
    reset();oversize=1;CHECK(gateway_network_observe(&observed)!=0&&closed==2&&!memcmp(&observed,&old,sizeof(old)));
    strcpy(route,"Iface Destination Gateway Flags RefCnt Use Metric Mask\neth2 00000000 0100000A 0003 0 0 0 00000000\n");
    reset();CHECK(gateway_network_observe(&observed)==0&&observed.unsupported==1U&&observed.lan[0].present&&closed==3);
    printf("network observation provider checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
