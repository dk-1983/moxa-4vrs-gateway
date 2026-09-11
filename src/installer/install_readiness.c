#define _GNU_SOURCE
#include "installer/install_readiness.h"
#include "config/gateway_persistence.h"
#include "network/gateway_network_service.h"
#include <sys/stat.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
static int owned(unsigned long inode,const unsigned long*sockets,unsigned int count){unsigned int i;for(i=0;i<count;i++)if(sockets[i]==inode)return 1;return 0;}
static int listener(unsigned int udp,const char*address,unsigned int port,const unsigned long*sockets,unsigned int count){
 FILE*f=fopen(udp?"/proc/net/udp":"/proc/net/tcp","r");char line[512];unsigned int rows=0;unsigned long ip;int found=0;
 if(!f||gateway_ipv4_parse(address,&ip)){if(f)fclose(f);return 0;}
 while(fgets(line,sizeof(line),f)){char*save=0,*token;unsigned int field=0,local_port=0,state=0;unsigned long local_ip=0,inode=0;if(++rows>4096){fclose(f);return 0;}
  for(token=strtok_r(line," \t\n",&save);token;token=strtok_r(0," \t\n",&save),field++){
   if(field==1)(void)sscanf(token,"%lx:%x",&local_ip,&local_port);
   if(field==3)(void)sscanf(token,"%x",&state);
   if(field==9){char*end;inode=strtoul(token,&end,10);if(*end)inode=0;}
  }
  if(field>=10&&local_ip==(unsigned long)htonl((uint32_t)ip)&&local_port==port&&state==(udp?7U:10U)&&owned(inode,sockets,count))found=1;
 }
 if(ferror(f))found=0;
 fclose(f);return found;
}
int install_readiness_at(const char*root,pid_t pid){
 gateway_persistent_config_t cfg;gateway_network_service_status_t service;gateway_network_profile_t profile;char encoded[GATEWAY_NETWORK_SNAPSHOT_MAX];size_t length;unsigned int have_profile=0;
 unsigned long sockets[128];unsigned int count=0,uarts[8]={0},i,seen=0;char directory[64],path[1024],target[1024];DIR*d;struct dirent*e;
 if(!root||strlen(root)>800||pid<=1)return -1;
 snprintf(path,sizeof(path),"%s/var/hda/4vrs/config/gateway.conf",root);
 if(gateway_config_load_file(path,&cfg)!=GATEWAY_CONFIG_OK)return -1;
 snprintf(path,sizeof(path),"%s/etc/4vrs-network",root);
 if(!gateway_network_service_query(gateway_network_service_production(),&service)&&gateway_network_store_boot(path,encoded,sizeof(encoded),&length)==GATEWAY_NET_STORE_OK&&!gateway_network_profile_decode(encoded,length,&profile))have_profile=1;
 snprintf(directory,sizeof(directory),"/proc/%ld/fd",(long)pid);d=opendir(directory);if(!d)return -1;
 for(;;){char*end;long fd;ssize_t n;struct stat s;errno=0;e=readdir(d);if(!e){if(errno){closedir(d);return -1;}break;}fd=strtol(e->d_name,&end,10);if(*end||fd<0)continue;if(++seen>128){closedir(d);return -1;}
  snprintf(path,sizeof(path),"%s/%ld",directory,fd);n=readlink(path,target,sizeof(target)-1);if(n<0)continue;target[n]=0;
  if(!strncmp(target,"socket:[",8)){unsigned long inode;char tail;if(sscanf(target+8,"%lu%c",&inode,&tail)==2&&tail==']'&&count<128)sockets[count++]=inode;}
  if(stat(path,&s))continue;
  if(S_ISCHR(s.st_mode))for(i=0;i<8;i++){struct stat tty;snprintf(target,sizeof(target),"%s/dev/ttyM%u",root,i);if(!stat(target,&tty)&&S_ISCHR(tty.st_mode)&&tty.st_rdev==s.st_rdev)uarts[i]=1;}
 }
 closedir(d);
 for(i=0;i<8;i++)if(cfg.ports[i].enabled){const char*address=cfg.ports[i].bind_address;unsigned int udp;
  if(have_profile&&profile.affinity[i]>=1&&profile.affinity[i]<=2&&!strcmp(profile.original_bind[i],address))address=service.effective.lan[profile.affinity[i]-1].address;
  udp=cfg.ports[i].transport!=TRANSPORT_MODBUS_TCP&&cfg.ports[i].transport!=TRANSPORT_RAW_TCP;
  if(!uarts[i]||!listener(udp,address,cfg.ports[i].endpoint_port,sockets,count))return -1;
 }
 return 0;
}
int install_readiness(pid_t pid){return install_readiness_at("",pid);}
