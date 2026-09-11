#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "network/gateway_network_service.h"
int __wrap_socket(int a,int b,int c){(void)a;(void)b;(void)c;return 1001;}
int __wrap_close(int fd){(void)fd;return 0;}
int __wrap_ioctl(int fd,unsigned long request,struct ifreq *r){const unsigned char mac[6]={0,0x90,0xe8,0x1f,0x4c,0xf1};const char *mode=getenv("PROBE_CASE");(void)fd;if(request!=SIOCGIFHWADDR)abort();memcpy(r->ifr_hwaddr.sa_data,mac,6);if(mode&&!strcmp(mode,"wrong-mac"))r->ifr_hwaddr.sa_data[5]=0;return 0;}
static void snapshot(gateway_network_observation_t *o){memset(o,0,sizeof(*o));o->lan[0].present=o->lan[0].up=o->lan[0].link=1;strcpy(o->lan[0].address,"10.0.2.13");o->lan[1].present=o->lan[1].up=1;}
int __wrap_gateway_network_service_query(const gateway_network_service_environment_t *e,gateway_network_service_status_t *s){const char *mode=getenv("PROBE_CASE");if(strcmp(e->directory,"/etc/4vrs-network")||e->write||e->observe)abort();if(mode&&!strcmp(mode,"query-error")){errno=ETIMEDOUT;return -1;}memset(s,0,sizeof(*s));snapshot(&s->observed);s->ready=s->settled=1;if(mode&&!strcmp(mode,"owner-error"))s->error=7;return 0;}
int __wrap_gateway_network_observe(gateway_network_observation_t *o){const char *mode=getenv("PROBE_CASE");if(mode&&!strcmp(mode,"direct-error")){errno=EIO;return -1;}snapshot(o);if(mode&&!strcmp(mode,"unsupported"))o->unsupported=2;return 0;}
