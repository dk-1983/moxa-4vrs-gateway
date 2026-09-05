#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "network/gateway_network_observation.h"

static int ip(char *to,const char *from)
{unsigned long a;if(gateway_ipv4_parse(from,&a))return -1;strcpy(to,from);return 0;}
static int decimal(const char *s,unsigned long *out)
{unsigned long n=0;unsigned int digits=0;while(*s){unsigned int d;if(*s<'0'||*s>'9'||++digits>10U)return -1;d=(unsigned int)(*s++-'0');if(n>(0xffffffffUL-d)/10UL)return -1;n=n*10UL+d;}if(!digits)return -1;*out=n;return 0;}
static int list(const char *s,char values[2][16],unsigned int maximum)
{unsigned int i=0;const char *p=s;while(*p){const char *end=strchr(p,',');size_t n=end?(size_t)(end-p):strlen(p);char b[16];if(!n||n>=16U||i>=maximum)return -1;memcpy(b,p,n);b[n]=0;if(ip(values[i++],b))return -1;if(!end)break;p=end+1;if(!*p)return -1;}return i?0:-1;}

int gateway_dhcp_lease_decode(const char *data,size_t length,unsigned int lan,gateway_dhcp_lease_t *out)
{
    gateway_dhcp_lease_t lease;gateway_network_settings_t s;unsigned int seen=0;
    size_t off=0;unsigned long a,m,b;char expected[16];
    if(!data||!out||lan>1U||!length||length>8192U||memchr(data,0,length))return -1;
    memset(&lease,0,sizeof(lease));snprintf(expected,sizeof(expected),"'eth%u'",lan);
    while(off<length){const char *end=memchr(data+off,'\n',length-off);size_t n=end?(size_t)(end-data-off):length-off;
        char line[512],*value;unsigned int bit=0;int r=0;
        if(n>=sizeof(line))return -1;
        memcpy(line,data+off,n);line[n]=0;off+=n+(end?1U:0U);
        value=strchr(line,'=');if(!value)continue;*value++=0;
        if(!strcmp(line,"IPADDR")){bit=1U;r=ip(lease.address,value);}
        else if(!strcmp(line,"NETMASK")){bit=2U;r=ip(lease.netmask,value);}
        else if(!strcmp(line,"BROADCAST")){bit=4U;r=ip(lease.broadcast,value);}
        else if(!strcmp(line,"GATEWAY")){char values[2][16]={{0},{0}};bit=8U;r=list(value,values,1);if(!r)strcpy(lease.gateway,values[0]);}
        else if(!strcmp(line,"DNS")){bit=16U;r=list(value,lease.dns,2);}
        else if(!strcmp(line,"LEASETIME")){bit=32U;r=decimal(value,&lease.lifetime);}
        else if(!strcmp(line,"INTERFACE")){bit=64U;r=strcmp(value,expected);}
        else if(!strcmp(line,"ROUTE")){return -1;} /* unowned routes cannot be silently applied */
        if(r||(bit&&(seen&bit)))return -1;
        seen|=bit;
    }
    if((seen&99U)!=99U||!lease.lifetime)return -1;
    gateway_network_settings_init(&s);s.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;
    strcpy(s.lan[0].address,lease.address);strcpy(s.lan[0].netmask,lease.netmask);
    strcpy(s.lan[0].gateway,lease.gateway);memcpy(s.dns,lease.dns,sizeof(s.dns));
    if(gateway_network_settings_validate(&s,0))return -1;
    if(gateway_ipv4_parse(lease.address,&a)||gateway_ipv4_parse(lease.netmask,&m))return -1;
    b=a|((~m)&0xffffffffUL);
    if(seen&4U){unsigned long supplied;if(gateway_ipv4_parse(lease.broadcast,&supplied)||supplied!=b)return -1;}
    else gateway_ipv4_format(b,lease.broadcast);
    *out=lease;return 0;
}

int gateway_network_routes_decode(const char *data,size_t length,gateway_network_observation_t *out)
{
    size_t off=0;unsigned int count=0,lan=0;char gateway[16]={0};
    if(!data||!out||length>16384U||memchr(data,0,length))return -1;
    while(off<length){char line[256],name[32];unsigned long dest,gw,flags,mask;unsigned int ref,use,metric;
        const char *end=memchr(data+off,'\n',length-off);size_t n=end?(size_t)(end-data-off):length-off;
        if(n>=sizeof(line))return -1;
        memcpy(line,data+off,n);line[n]=0;off+=n+(end?1U:0U);
        if(!strncmp(line,"Iface",5))continue;
        if(sscanf(line,"%31s %lx %lx %lx %u %u %u %lx",name,&dest,&gw,&flags,&ref,&use,&metric,&mask)!=8)return -1;
        if(dest==0UL&&mask==0UL&&(flags&1UL)){
            if(++count>1U)return -1;
            if(!strcmp(name,"eth0"))lan=1;else if(!strcmp(name,"eth1"))lan=2;else return -1;
            if(gw>0xffffffffUL)return -1;
            gateway_ipv4_format((unsigned long)ntohl((unsigned int)gw),gateway);
        }
    }
    out->default_routes=count;out->default_lan=lan;strcpy(out->gateway,gateway);return 0;
}

static int read_bounded(const char *path,char *b,size_t cap,size_t *length)
{int fd=open(path,O_RDONLY);size_t used=0;if(fd<0)return -1;while(used<cap){ssize_t n=read(fd,b+used,cap-used);if(n<0&&errno==EINTR)continue;if(n<0){close(fd);return -1;}if(!n)break;used+=(size_t)n;}if(close(fd)||used==cap)return -1;b[used]=0;*length=used;return 0;}
int gateway_network_observe(gateway_network_observation_t *out)
{
    gateway_network_observation_t result;int fd;unsigned int i;char data[16385];size_t n;
    if(!out)return -1;
    memset(&result,0,sizeof(result));fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
    for(i=0;i<2U;++i){struct ifreq req;unsigned int k;unsigned long requests[3]={SIOCGIFADDR,SIOCGIFNETMASK,SIOCGIFBRDADDR};
        char *values[3]={result.lan[i].address,result.lan[i].netmask,result.lan[i].broadcast};
        memset(&req,0,sizeof(req));snprintf(req.ifr_name,sizeof(req.ifr_name),"eth%u",i);
        if(ioctl(fd,SIOCGIFFLAGS,&req)){if(errno==ENODEV)continue;close(fd);return -1;}
        result.lan[i].present=1;result.lan[i].up=(req.ifr_flags&IFF_UP)!=0;result.lan[i].link=(req.ifr_flags&IFF_RUNNING)!=0;
        if(ioctl(fd,SIOCGIFHWADDR,&req)){close(fd);return -1;}memcpy(result.lan[i].mac,req.ifr_hwaddr.sa_data,6);
        for(k=0;k<3U;++k){struct sockaddr_in *address=(struct sockaddr_in*)&req.ifr_addr;
            if(ioctl(fd,requests[k],&req)){if(errno==EADDRNOTAVAIL)continue;close(fd);return -1;}
            gateway_ipv4_format((unsigned long)ntohl(address->sin_addr.s_addr),values[k]);
        }
    }
    close(fd);
    if(read_bounded("/proc/net/route",data,sizeof(data),&n))return -1;
    if(gateway_network_routes_decode(data,n,&result))result.unsupported|=1U;
    if(read_bounded("/etc/resolv.conf",data,sizeof(data),&n))return -1;
    {char *line=data;unsigned int count=0;while(line&&*line){char *end=strchr(line,'\n'),addr[16],extra;int matched;if(end)*end=0;
        matched=sscanf(line," nameserver %15s %c",addr,&extra);
        if(matched>0){
            if(matched!=1||count>=2U)result.unsupported|=2U;
            else if(ip(result.dns[count++],addr))result.unsupported|=2U;
        }
        line=end?end+1:0;
    }}
    *out=result;return 0;
}
