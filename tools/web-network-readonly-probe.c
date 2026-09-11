#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "network/gateway_network_service.h"
static int identity(void)
{
 const unsigned char expected[6]={0x00,0x90,0xe8,0x1f,0x4c,0xf1};
 struct ifreq r;int fd,result;memset(&r,0,sizeof(r));strcpy(r.ifr_name,"eth0");
 fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
 result=ioctl(fd,SIOCGIFHWADDR,&r);close(fd);
 if(result||memcmp(r.ifr_hwaddr.sa_data,expected,6))return -1;
 puts("identity.mac=00:90:E8:1F:4C:F1");return 0;
}
static void observation(const char *name,const gateway_network_observation_t *o)
{
 unsigned int i;printf("%s.unsupported=%u default_lan=%u default_routes=%u\n",name,o->unsupported,o->default_lan,o->default_routes);
 for(i=0;i<2;++i)printf("%s.eth%u present=%u up=%u link=%u address=%.15s\n",name,i,o->lan[i].present,o->lan[i].up,o->lan[i].link,o->lan[i].address);
}
int main(int argc,char **argv)
{
 gateway_network_service_environment_t service;gateway_network_service_status_t s;
 gateway_network_observation_t direct;int q,d,qe,de;
 if(argc!=2||strcmp(argv[1],"--moxa1-readonly")){fputs("usage: 4vrs-web-network-probe --moxa1-readonly\n",stderr);return 64;}
 alarm(5); /* Bound ordinary stalls; no timeout handler mutates the system. */
 if(identity()){fputs("identity mismatch/unavailable; probe refused\n",stderr);return 65;}
 memset(&service,0,sizeof(service));service.directory="/etc/4vrs-network";
 memset(&s,0,sizeof(s));errno=0;q=gateway_network_service_query(&service,&s);qe=errno;
 printf("query.result=%d errno=%d snapshot_bytes=%u\n",q,q?qe:0,(unsigned int)sizeof(s));
 if(!q){printf("owner protocol=%u error=%u ready=%u settled=%u generation=%u request=%u\n",s.protocol,s.error,s.ready,s.settled,s.generation,s.request);
  printf("owner dhcp0=%u lease0=%u dhcp1=%u lease1=%u\n",s.dhcp_state[0],s.lease_valid[0],s.dhcp_state[1],s.lease_valid[1]);observation("owner.observed",&s.observed);}
 memset(&direct,0,sizeof(direct));errno=0;d=gateway_network_observe(&direct);de=errno;
 printf("direct.result=%d errno=%d\n",d,d?de:0);if(!d)observation("direct",&direct);
 puts("scope=independent-readonly-sample; Gateway-private-observed_valid/observation_error-not-read; no-Web-health-claim");
 return q||d?2:0;
}
