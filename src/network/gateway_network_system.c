#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "network/gateway_network_system.h"
#include "network/gateway_network_document.h"

int gateway_network_file_replace(const char *path,const char *text)
{
    char tmp[256],dir[256],current[GATEWAY_NETWORK_FILE_MAX],*slash;
    struct stat st,t;int fd=-1,dfd=-1,r=-1;size_t size,used=0;ssize_t n;
    if(!path||!text||strlen(path)+12U>=sizeof(tmp))return -1;
    size=strnlen(text,sizeof(current));if(size>=sizeof(current))return -1;
    if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1)return -1;
    strcpy(dir,path);slash=strrchr(dir,'/');if(!slash)return -1;*slash=0;
    dfd=open(dir,O_RDONLY|O_DIRECTORY);if(dfd<0)return -1;
    fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)goto done;
    while(used<sizeof(current)){
        n=read(fd,current+used,sizeof(current)-used);
        if(n<0&&errno==EINTR)continue;
        if(n<0)goto done;
        if(!n)break;
        used+=(size_t)n;
    }
    close(fd);fd=-1;
    if(used==size&&!memcmp(current,text,size)){r=fsync(dfd)?-1:0;goto done;}
    snprintf(tmp,sizeof(tmp),"%s.4vrs.tmp",path);
    if(!lstat(tmp,&t)){
        if(!S_ISREG(t.st_mode)||t.st_uid!=geteuid()||t.st_nlink!=1||unlink(tmp))goto done;
    }else if(errno!=ENOENT)goto done;
    fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)goto done;
    used=0;
    while(used<size){n=write(fd,text+used,size-used);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;used+=(size_t)n;}
    if(fchown(fd,st.st_uid,st.st_gid)||fchmod(fd,st.st_mode&0777)||fsync(fd))goto done;
    if(close(fd)){fd=-1;goto done;}fd=-1;
    if(rename(tmp,path)||fsync(dfd))goto done;
    r=0;
done:if(fd>=0)close(fd);close(dfd);return r;
}
static int address(const char *text,unsigned long *value)
{if(!text[0]){*value=0;return 0;}return gateway_ipv4_parse(text,value);}
static void sa(struct sockaddr *out,unsigned long ip)
{struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(ip);memcpy(out,&a,sizeof(*out));}
static int route(int fd,unsigned long operation,unsigned int lan,const char *gateway)
{
    struct rtentry entry;char device[8];unsigned long ip;
    if(!lan)return 0;
    if(lan>2U||gateway_ipv4_parse(gateway,&ip))return -1;
    memset(&entry,0,sizeof(entry));sa(&entry.rt_dst,0);sa(&entry.rt_genmask,0);sa(&entry.rt_gateway,ip);
    snprintf(device,sizeof(device),"eth%u",lan-1U);entry.rt_dev=device;entry.rt_flags=RTF_UP|RTF_GATEWAY;
    return ioctl(fd,operation,&entry);
}
static int same(const gateway_lan_observation_t *a,const gateway_lan_observation_t *b)
{return a->up==b->up&&!strcmp(a->address,b->address)&&!strcmp(a->netmask,b->netmask)&&!strcmp(a->broadcast,b->broadcast);}
int gateway_network_system_write(const gateway_network_observation_t *desired,const char *path,const char *resolver)
{
    gateway_network_observation_t current;struct ifreq req,list[32];struct ifconf inventory;
    unsigned long ip[2],mask[2],broadcast[2];unsigned int i,j,count;int fd,r=-1,route_changed;
    if(!desired||!path||!resolver||desired->unsupported||desired->default_lan>2U||
       !memchr(desired->gateway,0,sizeof(desired->gateway))||
       gateway_network_observe(&current)||current.unsupported||current.default_lan>2U)return -1;
    for(i=0;i<2U;++i){
        if(!desired->lan[i].present||!current.lan[i].present||desired->lan[i].up>1U||
           !memchr(desired->lan[i].address,0,16)||!memchr(desired->lan[i].netmask,0,16)||
           !memchr(desired->lan[i].broadcast,0,16)||address(desired->lan[i].address,&ip[i])||
           address(desired->lan[i].netmask,&mask[i])||address(desired->lan[i].broadcast,&broadcast[i]))return -1;
        if(ip[i]){unsigned long inverse=(~mask[i])&0xffffffffUL;
            if(!(ip[i]>>24)||(ip[i]>>24)==127UL||(ip[i]>>24)>=224UL||!mask[i]||inverse<3UL||
               (inverse&(inverse+1UL))||!(ip[i]&inverse)||(ip[i]&inverse)==inverse||
               (broadcast[i]&mask[i])!=(ip[i]&mask[i]))return -1;
        }else if(mask[i]||broadcast[i])return -1;
    }
    if(desired->default_lan){unsigned int lan=desired->default_lan-1U;unsigned long gw,inverse=(~mask[lan])&0xffffffffUL;
        if(!ip[lan]||!desired->lan[lan].up||gateway_ipv4_parse(desired->gateway,&gw)||!gw||
           !(gw>>24)||(gw>>24)==127UL||(gw>>24)>=224UL||gw==ip[lan]||
           (gw&mask[lan])!=(ip[lan]&mask[lan])||!(gw&inverse)||(gw&inverse)==inverse)return -1;
    }else if(desired->gateway[0])return -1;
    if(ip[0]&&ip[1]&&((ip[0]&(mask[0]&mask[1]))==(ip[1]&(mask[0]&mask[1]))))return -1;
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
    memset(&inventory,0,sizeof(inventory));inventory.ifc_len=sizeof(list);inventory.ifc_req=list;
    if(ioctl(fd,SIOCGIFCONF,&inventory)||inventory.ifc_len<0||inventory.ifc_len>=(int)sizeof(list))goto done;
    count=(unsigned int)inventory.ifc_len/sizeof(list[0]);
    for(i=0;i<count;++i){struct sockaddr_in a;unsigned long other,other_mask;
        if(!strcmp(list[i].ifr_name,"lo")||!strcmp(list[i].ifr_name,"eth0")||!strcmp(list[i].ifr_name,"eth1"))continue;
        memcpy(&a,&list[i].ifr_addr,sizeof(a));other=ntohl(a.sin_addr.s_addr);if(!other)continue;
        req=list[i];if(ioctl(fd,SIOCGIFNETMASK,&req))goto done;
        memcpy(&a,&req.ifr_netmask,sizeof(a));other_mask=ntohl(a.sin_addr.s_addr);
        for(j=0;j<2U;++j)if(ip[j]&&(ip[j]&(mask[j]&other_mask))==(other&(mask[j]&other_mask)))goto done;
    }
    route_changed=current.default_lan!=desired->default_lan||strcmp(current.gateway,desired->gateway);
    if(current.default_lan&&!same(&current.lan[current.default_lan-1U],&desired->lan[current.default_lan-1U]))route_changed=1;
    if(route_changed&&route(fd,SIOCDELRT,current.default_lan,current.gateway))goto done;
    for(i=0;i<2U;++i){
        if(same(&current.lan[i],&desired->lan[i]))continue;
        memset(&req,0,sizeof(req));snprintf(req.ifr_name,sizeof(req.ifr_name),"eth%u",i);
        sa(&req.ifr_addr,ip[i]);if(ioctl(fd,SIOCSIFADDR,&req))goto done;
        if(ip[i]){
            sa(&req.ifr_netmask,mask[i]);if(ioctl(fd,SIOCSIFNETMASK,&req))goto done;
            sa(&req.ifr_broadaddr,broadcast[i]);if(ioctl(fd,SIOCSIFBRDADDR,&req))goto done;
        }
        if(ioctl(fd,SIOCGIFFLAGS,&req))goto done;
        if(desired->lan[i].up)req.ifr_flags|=IFF_UP;else req.ifr_flags&=(short)~IFF_UP;
        if(ioctl(fd,SIOCSIFFLAGS,&req))goto done;
    }
    if(route_changed&&route(fd,SIOCADDRT,desired->default_lan,desired->gateway))goto done;
    r=gateway_network_file_replace(path,resolver);
done:close(fd);return r;
}
