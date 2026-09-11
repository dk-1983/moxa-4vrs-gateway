#define _GNU_SOURCE
#include "installer/install_network.h"
#include "config/gateway_persistence.h"
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"network plan line %d: %s (%s)\n",__LINE__,#x,stage);exit(1);}}while(0)
static unsigned int checks;
static const char*stage="fixture";
static const char document[]="# own network\nauto eth0 eth1 eth2 lo\niface eth0 inet static\n address 10.20.2.7\n netmask 255.255.240.0\n broadcast 10.20.2.255\n gateway 10.20.0.1\n dns-servers 10.20.0.1 10.20.0.3\niface eth1 inet static\n address 192.168.37.127\n network 192.168.37.0\n netmask 255.255.255.0\n broadcast 192.168.37.255\niface eth2 inet static\n address 192.168.38.127\n netmask 255.255.255.0\n up echo retained-hook\niface lo inet loopback\n";
static const char resolver[]="# own resolver\nnameserver 10.20.0.1\nnameserver 10.20.0.3\n";
static int observe(void*c,gateway_network_observation_t*o){(void)c;memset(o,0,sizeof(*o));o->lan[0].present=o->lan[0].up=o->lan[1].present=o->lan[1].up=1;strcpy(o->lan[0].address,"10.20.2.7");strcpy(o->lan[0].netmask,"255.255.240.0");strcpy(o->lan[0].broadcast,"10.20.2.255");strcpy(o->lan[1].address,"192.168.37.127");strcpy(o->lan[1].netmask,"255.255.255.0");strcpy(o->lan[1].broadcast,"192.168.37.255");o->default_lan=o->default_routes=1;strcpy(o->gateway,"10.20.0.1");strcpy(o->dns[0],"10.20.0.1");strcpy(o->dns[1],"10.20.0.3");return 0;}
static int lan(void*c,gateway_lan_observation_t*l){gateway_network_observation_t o;observe(c,&o);*l=o.lan[1];return 0;}
static void put(const char*root,const char*name,const char*text){install_file_t f;f.kind=1;f.mode=0600;f.size=strlen(text);f.data=(unsigned char*)text;CHECK(!install_file_publish(root,name,&f));}
static void directory(const char*root,const char*name){char p[1024];CHECK(snprintf(p,sizeof(p),"%s/%s",root,name)>0);CHECK(!mkdir(p,0700));}
int main(int argc,char**argv){char root[1024],work[1024],config[GATEWAY_CONFIG_MAX_BYTES];gateway_persistent_config_t cfg;gateway_network_environment_t env;install_plan_t plan;install_file_t original,after;size_t n;unsigned int i;
 CHECK(argc==2);CHECK(snprintf(root,sizeof(root),"%s/network-XXXXXX",argv[1])>0);CHECK(mkdtemp(root)!=0);
 directory(root,"etc");directory(root,"etc/network");directory(root,"etc/4vrs-network");directory(root,"var");directory(root,"var/hda");directory(root,"var/hda/4vrs");directory(root,"var/hda/4vrs/config");directory(root,"work");directory(root,"work/store");
 CHECK(snprintf(work,sizeof(work),"%s/work",root)>0);
 put(root,"etc/network/interfaces",document);put(root,"etc/resolv.conf",resolver);
 gateway_persistent_defaults(&cfg);strcpy(cfg.ports[0].bind_address,"10.20.2.7");cfg.settings.ntp_enabled=1;strcpy(cfg.settings.ntp_server,"10.20.0.1");CHECK(gateway_config_encode(&cfg,config,sizeof(config),&n)==GATEWAY_CONFIG_OK);config[n]=0;put(root,"var/hda/4vrs/config/gateway.conf",config);
 memset(&env,0,sizeof(env));env.read_lan2=lan;env.read_network=observe;memset(&plan,0,sizeof(plan));
 CHECK(!install_network_plan(root,work,&env,&plan,&stage));CHECK(plan.count==5);
 CHECK(strstr((char*)plan.member[0].after.data," dns-nameservers 10.20.0.1 10.20.0.3\n")!=0);
 CHECK(plan.member[0].after.size==strlen(document)+4);
 CHECK(!install_file_read(root,"etc/network/interfaces",&original));CHECK(original.size==strlen(document)&&!memcmp(original.data,document,original.size));install_file_free(&original);
 CHECK(install_file_equal(&plan.member[1].before,&plan.member[1].after));{gateway_persistent_config_t old_cfg,new_cfg;CHECK(gateway_config_decode((char*)plan.member[2].before.data,plan.member[2].before.size,&old_cfg)==GATEWAY_CONFIG_OK);CHECK(gateway_config_decode((char*)plan.member[2].after.data,plan.member[2].after.size,&new_cfg)==GATEWAY_CONFIG_OK);CHECK(old_cfg.schema_version==1&&new_cfg.schema_version==1&&new_cfg.settings.web_enabled==0&&new_cfg.settings.web_interface==0&&new_cfg.settings.web_protocol==1);CHECK(!memcmp(&old_cfg,&new_cfg,sizeof(old_cfg)));}
 /* Simulate publication by the transaction engine's actual file provider,
  * then build an update plan from the resulting own confirmed/good files. */
 for(i=0;i<plan.count;i++)CHECK(!install_file_publish(root,plan.member[i].path,&plan.member[i].after));
 install_plan_free(&plan);
 directory(root,"update");directory(root,"update/store");CHECK(snprintf(work,sizeof(work),"%s/update",root)>0);
 CHECK(!install_network_plan(root,work,&env,&plan,&stage));for(i=0;i<plan.count;i++)CHECK(install_file_equal(&plan.member[i].before,&plan.member[i].after));install_plan_free(&plan);
 /* Explicit schema-2 choices survive byte-for-byte, including backlight Off. */
 for(i=0;i<18;i++){
  char name[32];unsigned int j;
  cfg.schema_version=i<6?2:3;cfg.settings.web_protocol=i<12?1:0;cfg.settings.web_enabled=(i%6)/3;cfg.settings.web_interface=i%3;cfg.settings.backlight_on=0;
  CHECK(gateway_config_encode(&cfg,config,sizeof(config),&n)==GATEWAY_CONFIG_OK);config[n]=0;put(root,"var/hda/4vrs/config/gateway.conf",config);
  snprintf(name,sizeof(name),"choice%u",i);directory(root,name);snprintf(name,sizeof(name),"choice%u/store",i);directory(root,name);
  CHECK(snprintf(work,sizeof(work),"%s/choice%u",root,i)>0);
  CHECK(!install_network_plan(root,work,&env,&plan,&stage));
  for(j=0;j<plan.count;j++)CHECK(install_file_equal(&plan.member[j].before,&plan.member[j].after));
  install_plan_free(&plan);
 }
 /* No saved configuration: HTTP enabled on LAN1, no secret payload. */
 {char cfgpath[1200];snprintf(cfgpath,sizeof(cfgpath),"%s/var/hda/4vrs/config/gateway.conf",root);CHECK(!unlink(cfgpath));}
 directory(root,"fresh");directory(root,"fresh/store");CHECK(snprintf(work,sizeof(work),"%s/fresh",root)>0);
 CHECK(!install_network_plan(root,work,&env,&plan,&stage));CHECK(plan.member[2].before.kind==0);
 CHECK(gateway_config_decode((char*)plan.member[2].after.data,plan.member[2].after.size,&cfg)==GATEWAY_CONFIG_OK);
 CHECK(cfg.schema_version==3&&cfg.settings.web_enabled==1&&cfg.settings.web_interface==0&&cfg.settings.web_protocol==0);
 install_plan_free(&plan);
 /* A conflicting resolver is refused without altering live fixture bytes. */
 directory(root,"conflict");directory(root,"conflict/store");CHECK(snprintf(work,sizeof(work),"%s/conflict",root)>0);put(root,"etc/resolv.conf","nameserver 10.20.0.9\n");
 CHECK(install_network_plan(root,work,&env,&plan,&stage)<0);CHECK(!install_file_read(root,"etc/resolv.conf",&after));CHECK(!strcmp((char*)after.data,"nameserver 10.20.0.9\n"));install_file_free(&after);install_plan_free(&plan);
 printf("installer production import/boot plan: %u checks passed; live providers replaced\n",checks);return 0;
}
