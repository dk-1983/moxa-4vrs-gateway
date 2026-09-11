#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "network/gateway_dhcp_io.h"
static unsigned int checks,failed,calls,fail_at,sockets,closes,binds;static char expected[8];
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("line %u: %s\n",(unsigned int)__LINE__,#x);}}while(0)
static int fault(void){if(++calls==fail_at){errno=EIO;return 1;}return 0;}
int __wrap_socket(int domain,int type,int protocol){(void)protocol;CHECK((domain==AF_INET&&type==SOCK_DGRAM)||(domain==PF_PACKET&&type==SOCK_RAW));if(fault())return -1;return 10+(int)sockets++;}
int __wrap_close(int fd){CHECK(fd==10||fd==11);++closes;return 0;}
int __wrap_ioctl(int fd,unsigned long request,void *arg){struct ifreq *r=arg;CHECK(fd==10&&request==SIOCGIFINDEX);CHECK(!strcmp(r->ifr_name,expected));if(fault())return -1;r->ifr_ifindex=7;return 0;}
int __wrap_fcntl(int fd,int cmd,...){CHECK(fd==10||fd==11);CHECK(cmd==F_GETFL||cmd==F_SETFL||cmd==F_SETFD);return fault()?-1:0;}
int __wrap_setsockopt(int fd,int level,int option,const void *data,socklen_t size){CHECK(fd==10||fd==11);CHECK(level==SOL_SOCKET);if(option==SO_BINDTODEVICE){CHECK(size==strlen(expected)+1U&&!strcmp(data,expected));}return fault()?-1:0;}
int __wrap_bind(int fd,const struct sockaddr *address,socklen_t length){(void)length;++binds;if(fd==10){const struct sockaddr_in *a=(const void *)address;CHECK(a->sin_family==AF_INET&&ntohs(a->sin_port)==68&&a->sin_addr.s_addr==0);}else{const struct sockaddr_ll *a=(const void *)address;CHECK(fd==11&&a->sll_family==AF_PACKET&&a->sll_ifindex==7);}return fault()?-1:0;}
ssize_t __wrap_recv(int fd,void *bytes,size_t n,int flags){(void)bytes;(void)n;CHECK(fd==11&&(flags&MSG_DONTWAIT)&&(flags&MSG_TRUNC));errno=EAGAIN;return -1;}
ssize_t __wrap_sendto(int fd,const void *bytes,size_t n,int flags,const struct sockaddr *address,socklen_t size){(void)fd;(void)bytes;(void)n;(void)flags;(void)address;(void)size;CHECK(0);return -1;}
int main(void){gateway_dhcp_io_t io;unsigned int total,i,lan;unsigned char bytes[1514];
 for(lan=0;lan<2U;++lan){snprintf(expected,sizeof(expected),"eth%u",lan);calls=sockets=closes=binds=fail_at=0;
  CHECK(!gateway_dhcp_io_open(&io,lan));total=calls;CHECK(sockets==2&&binds==2);CHECK(!gateway_dhcp_io_receive(&io,bytes,sizeof(bytes)));gateway_dhcp_io_close(&io);CHECK(closes==2&&io.packet==-1&&io.udp==-1);
  for(i=1;i<=total;++i){calls=sockets=closes=binds=0;fail_at=i;CHECK(gateway_dhcp_io_open(&io,lan)<0);CHECK(closes==sockets);CHECK(io.packet==-1&&io.udp==-1);}
 }
 printf("DHCP socket providers: %u checks, %u failed\n",checks,failed);return failed?1:0;}
