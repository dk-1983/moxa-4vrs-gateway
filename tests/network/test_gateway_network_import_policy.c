#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "network/gateway_network_import_policy.h"
#include "network/gateway_network_runtime.h"
#include "network/gateway_network_store.h"
static unsigned int checks,failed;static gateway_network_observation_t observed;
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("line %u: %s\n",(unsigned int)__LINE__,#x);}}while(0)
static int observe(void *c,gateway_network_observation_t *out){(void)c;*out=observed;return 0;}
static int lan2(void *c,gateway_lan_observation_t *out){(void)c;*out=observed.lan[1];return 0;}
static void file(const char *p,const char *text,size_t n){FILE *f=fopen(p,"wb");CHECK(f!=0);if(f){CHECK(fwrite(text,1,n,f)==n);CHECK(!fclose(f));}}
int main(void)
{
 char directory[]="/tmp/4vrs-import-XXXXXX",proc[256],client[256],leases[256],store[256],interfaces[256],resolver[256],path[300],bytes[32768];
 const char doc[]="auto lo eth0 eth1 eth2\niface lo inet loopback\niface eth0 inet static\n address 10.0.2.13\n netmask 255.255.240.0\n broadcast 10.0.2.255\n gateway 10.0.0.1\niface eth1 inet dhcp\niface eth2 inet manual\n up /vendor/hook\n";
 const char dns[]="# retain options\nnameserver 192.0.2.1\n";
 const char args[]="/sbin/dhcpcd\0-G\0eth1";
 const char info[]="IPADDR=192.0.2.100\nNETMASK=255.255.255.0\nBROADCAST=192.0.2.255\nGATEWAY=192.0.2.1\nDNS=192.0.2.1\nLEASETIME=60\nINTERFACE='eth1'\nHOSTNAME='$(never-execute)'\n";
 const char *bindings[8]={"0.0.0.0","192.0.2.100","10.0.2.13","0.0.0.0","0.0.0.0","0.0.0.0","0.0.0.0","0.0.0.0"};
 gateway_network_environment_t e;gateway_network_service_environment_t service;gateway_network_profile_t p;const char *stage;size_t n;unsigned int lan,d,r,i;
 CHECK(!gateway_network_vendor_arguments(args,sizeof(args),&lan,&d,&r)&&lan==1&&d==1&&!r);
 {const char unsafe[]="dhcpcd\0-T\0eth1";CHECK(gateway_network_vendor_arguments(unsafe,sizeof(unsafe),&lan,&d,&r)<0);}
 CHECK(mkdtemp(directory)!=0);
 snprintf(proc,sizeof(proc),"%s/proc",directory);snprintf(client,sizeof(client),"%s/proc/7",directory);snprintf(leases,sizeof(leases),"%s/leases",directory);snprintf(store,sizeof(store),"%s/store",directory);
 CHECK(!mkdir(proc,0700));CHECK(!mkdir(client,0700));CHECK(!mkdir(leases,0700));CHECK(!mkdir(store,0700));
 snprintf(interfaces,sizeof(interfaces),"%s/interfaces",directory);snprintf(resolver,sizeof(resolver),"%s/resolver",directory);
 file(interfaces,doc,strlen(doc));file(resolver,dns,strlen(dns));snprintf(path,sizeof(path),"%s/cmdline",client);file(path,args,sizeof(args));
 snprintf(path,sizeof(path),"%s/dhcpcd-eth1.info",leases);file(path,info,strlen(info));
 memset(&observed,0,sizeof(observed));for(i=0;i<2U;++i)observed.lan[i].up=observed.lan[i].present=1;
 strcpy(observed.lan[0].address,"10.0.2.13");strcpy(observed.lan[0].netmask,"255.255.240.0");strcpy(observed.lan[0].broadcast,"10.0.2.255");
 strcpy(observed.lan[1].address,"192.0.2.100");strcpy(observed.lan[1].netmask,"255.255.255.0");strcpy(observed.lan[1].broadcast,"192.0.2.255");
 observed.default_lan=observed.default_routes=1;strcpy(observed.gateway,"10.0.0.1");strcpy(observed.dns[0],"192.0.2.1");
 memset(&e,0,sizeof(e));memset(&service,0,sizeof(service));e.store_directory=store;e.interfaces_path=interfaces;e.resolver_path=resolver;e.read_lan2=lan2;e.read_network=observe;e.service=&service;service.directory=store;e.import_proc_directory=proc;e.import_lease_directory=leases;
 CHECK(!gateway_network_enroll_detailed(&e,bindings,&stage));CHECK(!strcmp(stage,"ok"));
 CHECK(!gateway_network_store_boot(store,bytes,sizeof(bytes),&n));CHECK(!gateway_network_profile_decode(bytes,n,&p));
 CHECK(p.settings.lan[1].mode==GATEWAY_LAN_DHCP_CLIENT&&!p.settings.lan[1].address[0]);
 CHECK(p.settings.default_lan==1&&p.settings.automatic_dns&&p.settings.dns_lan==2);
 CHECK(p.affinity[1]==2&&p.affinity[2]==1);CHECK(!strcmp(p.interfaces,doc)&&!strcmp(p.resolver,dns));
 CHECK(!gateway_network_boot_restore(&e));
 {FILE *f=fopen(resolver,"rb");CHECK(f!=0);if(f){n=fread(bytes,1,sizeof(bytes)-1U,f);bytes[n]=0;fclose(f);CHECK(!strcmp(bytes,"# retain options\n"));}}
 CHECK(!gateway_network_boot_restore(&e)); /* No remembered lease at reboot. */
 CHECK(gateway_network_enroll(&e,bindings)<0); /* Never overwrite confirmed. */
 strcpy(observed.dns[0],"192.0.2.2");CHECK(gateway_network_import_policy(proc,leases,&p.settings,&observed)<0);
 {const char *names[]={"confirmed","good","candidate","lock","write.tmp"};for(i=0;i<5U;++i){snprintf(path,sizeof(path),"%s/%s",store,names[i]);unlink(path);}}
 snprintf(path,sizeof(path),"%s/cmdline",client);unlink(path);snprintf(path,sizeof(path),"%s/dhcpcd-eth1.info",leases);unlink(path);
 unlink(interfaces);unlink(resolver);rmdir(client);rmdir(proc);rmdir(leases);rmdir(store);rmdir(directory);
 printf("DHCP enrollment: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
