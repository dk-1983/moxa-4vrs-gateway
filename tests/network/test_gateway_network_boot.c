#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include "network/gateway_network_boot.h"
static unsigned int checks,failed,calls,fail_at;static char names[32][16];
#define CHECK(x) do{++checks;if(!(x)){++failed;fprintf(stderr,"line %u: %s\n",(unsigned int)__LINE__,#x);}}while(0)
static int action(void *context,const char *name,unsigned int up)
{CHECK(context==names);CHECK(up<=1U);strcpy(names[calls++],name);return calls==fail_at?-1:0;}
static int eth2_present,inventory_error,missing_lo,eth2_state,lo_up,hook_error,diag_count,saw_addressless_up,saw_observation_error,saw_wrong_state;
static int present(void *c,const char *n){(void)c;if(inventory_error)return -1;if(missing_lo&&!strcmp(n,"lo"))return 0;return strcmp(n,"eth2")?1:eth2_present;}
static int vendor(void *c,const char *n,unsigned int up)
{(void)c;(void)up;++calls;if(!strcmp(n,"lo")){lo_up=1;return 0;}eth2_state=1;return hook_error;}
static void diagnostic(void *c,const char *n,const char *stage,int result)
{(void)c;(void)n;(void)result;++diag_count;if(!strcmp(stage,"lo-no-address-up"))saw_addressless_up=1;if(!strcmp(stage,"lo-observation-error"))saw_observation_error=1;if(!strcmp(stage,"lo-address-mask-state"))saw_wrong_state=1;}
static int socket_error,ioctl_error,address_error,mask_error;
static int lo_flags=IFF_LOOPBACK|IFF_UP;
static unsigned long lo_address=0x7f000001UL,lo_mask=0xff000000UL;
int __real_close(int);
int __wrap_socket(int domain,int type,int protocol){CHECK(domain==AF_INET&&type==SOCK_DGRAM&&protocol==0);return socket_error?-1:9001;}
int __wrap_close(int fd){return fd==9001?0:__real_close(fd);}
int __wrap_ioctl(int fd,unsigned long request,...)
{va_list args;struct ifreq *ifr;CHECK(fd==9001);va_start(args,request);ifr=va_arg(args,struct ifreq *);va_end(args);if(ioctl_error){errno=ioctl_error;return -1;}
 if(request==SIOCGIFADDR&&address_error){errno=address_error;return -1;}
 if(request==SIOCGIFNETMASK&&mask_error){errno=mask_error;return -1;}
 if(request==SIOCGIFINDEX)ifr->ifr_ifindex=3;
 else if(request==SIOCGIFFLAGS)ifr->ifr_flags=lo_flags;
 else if(request==SIOCGIFADDR||request==SIOCGIFNETMASK){struct sockaddr_in *a=(struct sockaddr_in *)&ifr->ifr_addr;a->sin_family=AF_INET;a->sin_addr.s_addr=htonl(request==SIOCGIFADDR?lo_address:lo_mask);}
 else CHECK(0);
 return 0;}
static int loop_state,receipt_state,loop_calls,loop_error,receipt_error,observation_error,no_change;
static int loop_observe(void *c){(void)c;return observation_error?-1:loop_state;}
static int loop_receipt(void *c,unsigned int op,const char *s,size_t n)
{(void)c;(void)s;(void)n;if(receipt_error)return -1;if(op)receipt_state=op==2;return op?0:receipt_state;}
static int loop_action(void *c,unsigned int up){(void)c;++loop_calls;if(!loop_error&&!no_change)loop_state=up?1:3;return loop_error;}
static void loop_tests(void)
{
 const gateway_network_loopback_ops_t ops={loop_observe,loop_receipt,loop_action,diagnostic};
 const char *text="auto lo\niface lo inet loopback\n up /vendor/hook\n";
 size_t n=strlen(text);
 /* Existing address without proof of hooks is NOT adopted, even with UP. */
 loop_state=1;CHECK(gateway_network_loopback(text,n,1,&ops,0)<0);CHECK(!loop_calls);
 CHECK(!gateway_network_loopback(text,n,0,&ops,0));CHECK(loop_state==3&&!receipt_state);
 CHECK(!gateway_network_loopback(text,n,1,&ops,0));CHECK(loop_calls==2&&receipt_state);
 CHECK(!gateway_network_loopback(text,n,1,&ops,0));CHECK(loop_calls==2);
 loop_state=2;CHECK(gateway_network_loopback(text,n,1,&ops,0)<0);CHECK(loop_calls==2);
 observation_error=1;CHECK(gateway_network_loopback(text,n,0,&ops,0)<0);CHECK(loop_calls==2);observation_error=0;
 loop_state=0;loop_error=7;CHECK(gateway_network_loopback(text,n,1,&ops,0)==7);CHECK(!receipt_state);
 loop_error=-2;CHECK(gateway_network_loopback(text,n,1,&ops,0)==-2);CHECK(!receipt_state);
 loop_error=0;receipt_error=1;CHECK(gateway_network_loopback(text,n,1,&ops,0)<0);CHECK(loop_calls==4);receipt_error=0;
 CHECK(!gateway_network_loopback(text,n,1,&ops,0));CHECK(receipt_state);
 /* A false vendor success (e.g. stale ifstate) must not authorize adoption. */
 loop_state=0;no_change=1;CHECK(gateway_network_loopback(text,n,1,&ops,0)<0);CHECK(!receipt_state);no_change=0;
 loop_state=1;receipt_state=1;no_change=1;
 CHECK(gateway_network_loopback(text,n,0,&ops,0)<0);CHECK(!receipt_state);no_change=0;
 loop_state=3;loop_error=7;CHECK(gateway_network_loopback(text,n,1,&ops,0)==7);CHECK(!receipt_state);
 loop_error=0;CHECK(!gateway_network_loopback(text,n,1,&ops,0));CHECK(loop_state==1&&receipt_state);
 CHECK(saw_addressless_up&&saw_observation_error&&saw_wrong_state);
 socket_error=ioctl_error=0;CHECK(gateway_network_loopback_observe(0)==1);
 lo_flags=IFF_LOOPBACK;CHECK(gateway_network_loopback_observe(0)==2);
 lo_address=0;CHECK(gateway_network_loopback_observe(0)==0);
 lo_flags|=IFF_UP;CHECK(gateway_network_loopback_observe(0)==3);
 address_error=EADDRNOTAVAIL;CHECK(gateway_network_loopback_observe(0)==3);
 lo_flags=IFF_LOOPBACK;CHECK(gateway_network_loopback_observe(0)==0);
 address_error=EIO;CHECK(gateway_network_loopback_observe(0)<0);address_error=0;
 lo_flags=IFF_UP;CHECK(gateway_network_loopback_observe(0)<0);lo_flags=IFF_UP|IFF_LOOPBACK;
 lo_address=0x7f000002UL;CHECK(gateway_network_loopback_observe(0)==2);
 lo_address=0x7f000001UL;lo_mask=0xffff0000UL;CHECK(gateway_network_loopback_observe(0)==2);
 mask_error=EIO;CHECK(gateway_network_loopback_observe(0)<0);mask_error=0;
 ioctl_error=EIO;CHECK(gateway_network_loopback_observe(0)<0);ioctl_error=0;
 socket_error=1;CHECK(gateway_network_loopback_observe(0)<0);
}
static void checked_boot(void)
{
 const char *text="auto eth0 eth1 eth2 lo\niface eth2 inet static\n address 192.168.5.127\n netmask 255.255.255.0\n up /vendor/hook\niface lo inet loopback\n";
 const gateway_network_boot_ops_t ops={present,vendor,diagnostic,0};
 calls=0;CHECK(!gateway_network_unowned_checked(text,strlen(text),1,&ops,0));CHECK(lo_up&&calls==1&&!eth2_state);
 lo_up=0;calls=0;CHECK(!gateway_network_unowned_checked(text,strlen(text),1,&ops,0));CHECK(lo_up&&calls==1&&!eth2_state);
 eth2_present=1;lo_up=0;calls=0;CHECK(!gateway_network_unowned_checked(text,strlen(text),1,&ops,0));CHECK(lo_up&&calls==2&&eth2_state);
 hook_error=7;lo_up=0;calls=0;CHECK(gateway_network_unowned_checked(text,strlen(text),1,&ops,0)<0);CHECK(lo_up&&calls==2);
 inventory_error=1;calls=0;CHECK(gateway_network_unowned_checked(text,strlen(text),1,&ops,0)<0);CHECK(!calls);
 inventory_error=0;calls=0;CHECK(gateway_network_unowned_checked("auto lo\nsource /unknown\n",strlen("auto lo\nsource /unknown\n"),1,&ops,0)<0);CHECK(!calls);
 eth2_present=0;hook_error=0;lo_up=0;calls=0;
 {const char *manual="auto eth0 eth1 eth2 lo\niface eth2 inet manual\niface lo inet loopback\n";
 CHECK(gateway_network_unowned_checked(manual,strlen(manual),1,&ops,0)<0);CHECK(lo_up&&calls==1);}
 missing_lo=1;calls=0;CHECK(gateway_network_unowned_checked(text,strlen(text),1,&ops,0)<0);CHECK(!calls);missing_lo=0;
 CHECK(diag_count>0);
 CHECK(gateway_network_vendor_present(0,"eth2")==1);
 ioctl_error=ENODEV;CHECK(gateway_network_vendor_present(0,"eth2")==0);
 ioctl_error=ENXIO;CHECK(gateway_network_vendor_present(0,"eth2")==0);
 ioctl_error=EPERM;CHECK(gateway_network_vendor_present(0,"eth2")<0);
 ioctl_error=EIO;CHECK(gateway_network_vendor_present(0,"eth2")<0);
 socket_error=1;CHECK(gateway_network_vendor_present(0,"eth2")<0);
}
static void process_tests(void)
{
 char path[]="/tmp/4vrs-boot-action-XXXXXX",script[512];int fd=mkstemp(path),sentinel=open("/dev/null",O_RDONLY);FILE *f;int status;
 CHECK(fd>=0&&sentinel>2);f=fdopen(fd,"w");CHECK(f!=0);
 CHECK(gateway_network_loopback_receipt(&fd,0,"profile",7)==0);
 CHECK(gateway_network_loopback_receipt(&fd,2,"profile",7)==0);
 CHECK(gateway_network_loopback_receipt(&fd,0,"profile",7)==1);
 CHECK(gateway_network_loopback_receipt(&fd,0,"changed",7)==0);
 CHECK(gateway_network_loopback_receipt(&fd,1,"profile",7)==0);
 CHECK(gateway_network_loopback_receipt(&fd,0,"profile",7)==0);
 snprintf(script,sizeof(script),"#!/bin/sh\nif test -e /proc/self/fd/%d; then exit 9; fi\ncase \"$1\" in -f) test \"$2\" = lo; exit $?;; fail) exit 7;; hang) sleep 20;; *) exit 0;; esac\n",sentinel);
 CHECK(fputs(script,f)>=0&&fclose(f)==0);CHECK(chmod(path,0700)==0);
 CHECK(gateway_network_vendor_execute(path,"lo",2000)==0);
 CHECK(gateway_network_vendor_execute_forced(path,"lo",2000)==0);
 CHECK(gateway_network_vendor_execute(path,"fail",2000)==7);
 CHECK(gateway_network_vendor_execute(path,"hang",10)==-2);
 CHECK(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD);
 close(sentinel);unlink(path);
}
int main(void)
{
 const char text[]="# preserved\nauto lo eth0 eth1 eth2\nauto eth2\niface lo inet loopback\niface eth0 inet dhcp\niface eth1 inet static\n address 192.0.2.127\niface eth2 inet static\n up /vendor/hook\niface spare inet manual\n";
 const char bad[]="auto lo eth2\nauto bad;command\n";
 CHECK(!gateway_network_unowned(text,strlen(text),1,action,names));
 CHECK(calls==2&&!strcmp(names[0],"lo")&&!strcmp(names[1],"eth2"));
 calls=0;CHECK(!gateway_network_unowned(text,strlen(text),0,action,names));
 CHECK(calls==3&&!strcmp(names[2],"spare"));
 calls=0;CHECK(gateway_network_unowned(bad,strlen(bad),1,action,names)<0);CHECK(!calls);
 fail_at=1;CHECK(gateway_network_unowned(text,strlen(text),1,action,names)<0);CHECK(calls==2);
 CHECK(gateway_network_unowned(0,0,1,action,names)<0);
 checked_boot();loop_tests();process_tests();
 printf("network boot inventory: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
