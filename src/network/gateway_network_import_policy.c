#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "network/gateway_network_import_policy.h"
int gateway_network_vendor_arguments(const char *bytes,size_t n,unsigned int *lan,unsigned int *dns,unsigned int *route)
{
 size_t off,j;unsigned int found=0;const char *name;
 if(!bytes||!n||n>511U||bytes[n-1U]||!lan||!dns||!route)return -1;
 name=strrchr(bytes,'/');name=name?name+1:bytes;if(strcmp(name,"dhcpcd"))return -1;
 *dns=*route=1;*lan=0;off=strlen(bytes)+1U;
 while(off<n){const char *arg=bytes+off;off+=strlen(arg)+1U;
  if(!strcmp(arg,"eth0")||!strcmp(arg,"eth1")){if(found++)return -1;*lan=(unsigned int)(arg[3]-'0');continue;}
  if(arg[0]!='-'||!arg[1])return -1;
  for(j=1;arg[j];++j){
   if(arg[j]=='R')*dns=0;
   else if(strchr("dNYBCS",arg[j])){}
   else if(arg[j]=='G'){if(arg[j+1])return -1;*route=0;}
   else if(arg[j]=='t'||arg[j]=='l'){
    const char *number;if(arg[j+1]||off>=n)return -1;number=bytes+off;
    if(!*number||strlen(number)>10U)return -1;
    while(*number){if(*number<'0'||*number>'9')return -1;++number;}off+=strlen(bytes+off)+1U;
   }else return -1; /* Includes -V/-T/-c/-L and custom identities/hooks. */
  }
 }
 return found==1U?0:-1;
}
static int read_data(const char *path,char *bytes,size_t cap,size_t *length)
{
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);ssize_t n;if(fd<0)return -1;
 n=read(fd,bytes,cap);close(fd);if(n<0||(size_t)n==cap)return -1;*length=(size_t)n;return 0;
}
int gateway_network_import_policy(const char *proc,const char *leases,gateway_network_settings_t *settings,const gateway_network_observation_t *live)
{
 DIR *directory;struct dirent *entry;unsigned int count=0,seen=0,dns_source=0,route_sources=0;int result=-1;
 gateway_network_settings_t s;if(!proc||!leases||!settings||!live||live->unsupported)return -1;s=*settings;
 directory=opendir(proc);if(!directory)return -1;
 for(;;){char path[256],args[512],*end,*name;long pid;size_t n;unsigned int lan,dns,route;int size;
  errno=0;entry=readdir(directory);if(!entry){if(errno)goto done;break;}
  if(++count>1024U)goto done;
  pid=strtol(entry->d_name,&end,10);if(*end||pid<=0)continue;
  size=snprintf(path,sizeof(path),"%s/%s/cmdline",proc,entry->d_name);if(size<0||(size_t)size>=sizeof(path))goto done;
  if(read_data(path,args,sizeof(args)-1U,&n)){if(errno==ENOENT||errno==ESRCH)continue;goto done;}
  if(!n)continue;
  args[n]=0;if(!memchr(args,0,n))goto done;name=strrchr(args,'/');name=name?name+1:args;
  if(!strstr(name,"dhcpcd")&&!strstr(name,"udhcpc")&&!strstr(name,"dhclient")){
   if(!strcmp(name,"busybox")&&strlen(args)+1U<n&&!strcmp(args+strlen(args)+1U,"udhcpc"))goto done;
   continue;
  }
  if(gateway_network_vendor_arguments(args,n,&lan,&dns,&route)||s.lan[lan].mode!=GATEWAY_LAN_DHCP_CLIENT||(seen&(1U<<lan)))goto done;
  seen|=1U<<lan;
  {char lease[8193];gateway_dhcp_lease_t parsed;
   size=snprintf(path,sizeof(path),"%s/dhcpcd-eth%u.info",leases,lan);if(size<0||(size_t)size>=sizeof(path))goto done;
   if(read_data(path,lease,sizeof(lease)-1U,&n)||gateway_dhcp_lease_decode(lease,n,lan,&parsed))goto done;
   if(strcmp(parsed.address,live->lan[lan].address)||strcmp(parsed.netmask,live->lan[lan].netmask)||strcmp(parsed.broadcast,live->lan[lan].broadcast))goto done;
   if(dns){if(dns_source||strcmp(parsed.dns[0],live->dns[0])||strcmp(parsed.dns[1],live->dns[1]))goto done;dns_source=lan+1U;}
   if(route){++route_sources;if(live->default_lan!=lan+1U||strcmp(parsed.gateway,live->gateway))goto done;}
  }
 }
 {unsigned int i;for(i=0;i<2U;++i)if(s.lan[i].mode==GATEWAY_LAN_DHCP_CLIENT&&!(seen&(1U<<i)))goto done;}
 if(route_sources>1U)goto done;
 if(live->default_lan&&s.lan[live->default_lan-1U].mode==GATEWAY_LAN_DHCP_CLIENT){if(route_sources!=1U)goto done;s.default_lan=live->default_lan;}
 else if(live->default_lan!=s.default_lan)goto done;
 s.automatic_dns=dns_source?1U:0U;s.dns_lan=dns_source;
 if(gateway_network_settings_validate(&s,0))goto done;
 *settings=s;result=0;
 done:closedir(directory);return result;
}
