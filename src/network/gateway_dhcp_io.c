#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/filter.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "network/gateway_dhcp_io.h"
static int nonblocking(int fd)
{int flags=fcntl(fd,F_GETFL,0);return flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0||fcntl(fd,F_SETFD,FD_CLOEXEC)<0?-1:0;}
void gateway_dhcp_io_close(gateway_dhcp_io_t *io)
{if(!io)return;if(io->packet>=0)close(io->packet);if(io->udp>=0)close(io->udp);io->packet=io->udp=-1;}
int gateway_dhcp_io_open(gateway_dhcp_io_t *io,unsigned int lan)
{
    struct ifreq request;struct sockaddr_ll ll;struct sockaddr_in local;char device[8];int yes=1;
    struct sock_filter filter[]={
        BPF_STMT(BPF_LD|BPF_H|BPF_ABS,12),BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x0806,8,0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x0800,0,8),BPF_STMT(BPF_LD|BPF_B|BPF_ABS,23),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,17,0,6),BPF_STMT(BPF_LDX|BPF_B|BPF_MSH,14),
        BPF_STMT(BPF_LD|BPF_H|BPF_IND,14),BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,67,0,3),
        BPF_STMT(BPF_LD|BPF_H|BPF_IND,16),BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,68,0,1),
        BPF_STMT(BPF_RET|BPF_K,1514),BPF_STMT(BPF_RET|BPF_K,0)};
    struct sock_fprog program;int receive_bytes=16384;
    if(!io||lan>=2U)return -1;
    io->packet=io->udp=-1;io->index=0;
    snprintf(device,sizeof(device),"eth%u",lan);
    io->udp=socket(AF_INET,SOCK_DGRAM,0);if(io->udp<0)return -1;
    memset(&request,0,sizeof(request));strcpy(request.ifr_name,device);
    if(ioctl(io->udp,SIOCGIFINDEX,&request)||request.ifr_ifindex<=0)goto fail;
    io->index=request.ifr_ifindex;
    /* SO_BINDTODEVICE confines unicast renewals and UDP port ownership. The
     * raw socket handles pre-address broadcasts; no server socket is opened. */
    if(setsockopt(io->udp,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes))||
       setsockopt(io->udp,SOL_SOCKET,SO_RCVBUF,&receive_bytes,sizeof(receive_bytes))||
       setsockopt(io->udp,SOL_SOCKET,SO_BINDTODEVICE,device,strlen(device)+1U)||nonblocking(io->udp))goto fail;
    memset(&local,0,sizeof(local));local.sin_family=AF_INET;local.sin_port=htons(68);
    if(bind(io->udp,(struct sockaddr *)&local,sizeof(local)))goto fail;
    io->packet=socket(PF_PACKET,SOCK_RAW,htons(ETH_P_ALL));if(io->packet<0||nonblocking(io->packet))goto fail;
    program.len=(unsigned short)(sizeof(filter)/sizeof(filter[0]));program.filter=filter;
    if(setsockopt(io->packet,SOL_SOCKET,SO_ATTACH_FILTER,&program,sizeof(program))||
       setsockopt(io->packet,SOL_SOCKET,SO_RCVBUF,&receive_bytes,sizeof(receive_bytes)))goto fail;
    memset(&ll,0,sizeof(ll));ll.sll_family=AF_PACKET;ll.sll_protocol=htons(ETH_P_ALL);ll.sll_ifindex=io->index;
    if(bind(io->packet,(struct sockaddr *)&ll,sizeof(ll)))goto fail;
    return 0;
fail:gateway_dhcp_io_close(io);return -1;
}
int gateway_dhcp_io_receive(gateway_dhcp_io_t *io,unsigned char *out,size_t cap)
{
    ssize_t n;if(!io||io->packet<0||!out||cap>1514U)return -1;
    n=recv(io->packet,out,cap,MSG_DONTWAIT|MSG_TRUNC);
    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    if(n<0)return -1;
    return (size_t)n>cap?0:(int)n;
}
static int raw_send(gateway_dhcp_io_t *io,const unsigned char *frame,size_t n)
{
    struct sockaddr_ll target;ssize_t result;
    memset(&target,0,sizeof(target));target.sll_family=AF_PACKET;target.sll_ifindex=io->index;
    target.sll_halen=6;memset(target.sll_addr,255,6);target.sll_protocol=htons((unsigned short)(((unsigned int)frame[12]<<8)|frame[13]));
    result=sendto(io->packet,frame,n,MSG_DONTWAIT,(struct sockaddr *)&target,sizeof(target));
    return result==(ssize_t)n?0:-1;
}
int gateway_dhcp_io_send(gateway_dhcp_io_t *io,gateway_dhcp_client_t *client,core_tick_t now)
{
    unsigned char frame[600];size_t n;
    if(!io||!client||io->packet<0||io->udp<0)return -1;
    if(client->state==GATEWAY_DHCP_RENEWING){
        struct sockaddr_in server;ssize_t sent;
        n=gateway_dhcp_packet(client,frame,sizeof(frame),now);if(!n)return 0;
        memset(&server,0,sizeof(server));server.sin_family=AF_INET;server.sin_port=htons(67);server.sin_addr.s_addr=htonl(client->server);
        sent=sendto(io->udp,frame,n,MSG_DONTWAIT,(struct sockaddr *)&server,sizeof(server));return sent==(ssize_t)n?0:-1;
    }
    n=gateway_dhcp_broadcast(client,frame,sizeof(frame),now);return n?raw_send(io,frame,n):0;
}
int gateway_dhcp_io_probe(gateway_dhcp_io_t *io,const gateway_dhcp_client_t *client,unsigned int announce)
{unsigned char frame[42];size_t n=gateway_dhcp_arp(client,frame,sizeof(frame),announce);return n?raw_send(io,frame,n):-1;}
int gateway_dhcp_foreign_client(void)
{
    DIR *directory=opendir("/proc");struct dirent *entry;unsigned int count=0;int result=0;
    if(!directory)return -1;
    for(;;){char path[64],bytes[512],*end,*name;long pid;int fd;ssize_t n;
        errno=0;entry=readdir(directory);if(!entry){if(errno)result=-1;break;}
        if(++count>1024U){result=-1;break;}
        pid=strtol(entry->d_name,&end,10);if(*end||pid<=0)continue;
        snprintf(path,sizeof(path),"/proc/%ld/cmdline",pid);fd=open(path,O_RDONLY|O_NONBLOCK);
        if(fd<0){if(errno==ENOENT||errno==ESRCH)continue;result=-1;break;}
        n=read(fd,bytes,sizeof(bytes)-1U);close(fd);
        if(n<0){if(errno==ENOENT||errno==ESRCH)continue;result=-1;break;}
        if(!n)continue;
        bytes[n]=0;
        if(!memchr(bytes,0,(size_t)n)){result=-1;break;}
        name=strrchr(bytes,'/');name=name?name+1:bytes;
        /* The known target client plus common alternative names. A command
         * running a shell wrapper is not evidence that its child is ours. */
        if(strstr(name,"dhcpcd")||strstr(name,"udhcpc")||strstr(name,"dhclient")){result=1;break;}
        if(!strcmp(name,"busybox")){size_t at=strlen(bytes)+1U;
            if(at<(size_t)n&&!strcmp(bytes+at,"udhcpc")){result=1;break;}
        }
    }
    closedir(directory);return result;
}
