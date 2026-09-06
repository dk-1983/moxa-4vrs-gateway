#include "installer/install_network.h"
#include "config/gateway_persistence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int normalize(install_file_t*f,const install_file_t*r){
 gateway_network_settings_t settings;char *start,*end,*line,*key=0;unsigned int count=0,owner=0;size_t prefix;
 if(f->kind!=1||r->kind!=1||f->size>=GATEWAY_NETWORK_FILE_MAX||r->size>=GATEWAY_NETWORK_FILE_MAX||memchr(f->data,0,f->size)||memchr(r->data,0,r->size))return -1;
 if(!gateway_network_import((char*)f->data,f->size,(char*)r->data,r->size,&settings))return 0;
 /* A product migration, not a claim that Moxa executes dns-servers. It is
  * allowed only in eth0 static and the existing importer must validate exact
  * ordered resolver agreement after the single key substitution. */
 line=(char*)f->data;
 while(*line){end=strchr(line,'\n');if(!end)return -1;start=line;while(start<end&&(*start==' '||*start=='\t'))start++;
  if(!strncmp(start,"iface ",6)||!strncmp(start,"iface\t",6)){char row[256],iface[32],inet[32],mode[32],extra[32];int fields;if(end-start>=256)return -1;memcpy(row,start,(size_t)(end-start));row[end-start]=0;fields=sscanf(row,"iface %31s %31s %31s %31s",iface,inet,mode,extra);owner=fields==3&&!strcmp(iface,"eth0")&&!strcmp(inet,"inet")&&!strcmp(mode,"static");}
  if(!strncmp(start,"auto ",5)||!strncmp(start,"allow-hotplug ",14))owner=0;
  if(!strncmp(start,"dns-",4)){if(!owner||end-start<12||strncmp(start,"dns-servers",11)||(start[11]!=' '&&start[11]!='\t'))return -1;key=start;count++;}
  line=end+1;
 }
 if(count!=1||!key||f->size+4>=GATEWAY_NETWORK_FILE_MAX)return -1;
 prefix=(size_t)(key-(char*)f->data);
 {unsigned char *new_data=malloc(f->size+5);if(!new_data)return -1;memcpy(new_data,f->data,prefix);memcpy(new_data+prefix,"dns-nameservers",15);memcpy(new_data+prefix+15,key+11,f->size-prefix-11+1);free(f->data);f->data=new_data;f->size+=4;}
 return gateway_network_import((char*)f->data,f->size,(char*)r->data,r->size,&settings)?-1:1;
}
static int add(install_plan_t*p,const char*root,const char*path,unsigned int cf,const install_file_t*f){return install_plan_add(p,root,path,cf,f);}
int install_network_plan(const char*root,const char*work,const gateway_network_environment_t*providers,install_plan_t*p,const char**stage){
 static const char*names[]={"confirmed","good"};
 install_file_t interfaces={0,0,0,0},resolver={0,0,0,0},config={0,0,0,0},f={0,0,0,0};
 gateway_network_environment_t env;gateway_persistent_config_t cfg;const char*bindings[8];unsigned int i;int result=-1,existing=0;char path[256],ipath[256],rpath[256],store[256],buf[GATEWAY_CONFIG_MAX_BYTES];size_t n;
 *stage="network-inputs";
 for(i=0;i<2;i++){
  if(install_file_read(root,i?"etc/4vrs-network/commit.guard":"etc/4vrs-network/candidate",&f))goto done;
  if(f.kind){*stage="network-pending-transaction";goto done;}
 }
 if(install_file_read(root,"etc/network/interfaces",&interfaces)||install_file_read(root,"etc/resolv.conf",&resolver)||install_file_read(root,"var/hda/4vrs/config/gateway.conf",&config))goto done;
 *stage="network-dns-migration";if(normalize(&interfaces,&resolver)<0)goto done;
 *stage="configuration";
 if(config.kind){if(config.kind!=1||gateway_config_decode((char*)config.data,config.size,&cfg)!=GATEWAY_CONFIG_OK)goto done;}
 else{gateway_persistent_defaults(&cfg);if(gateway_config_encode(&cfg,buf,sizeof(buf),&n)!=GATEWAY_CONFIG_OK)goto done;config.kind=1;config.mode=0600;config.size=n;config.data=malloc(n+1);if(!config.data)goto done;memcpy(config.data,buf,n);config.data[n]=0;}
 *stage="network-shadow";
 if(strlen(work)+20>=sizeof(path)||install_file_publish(work,"interfaces",&interfaces)||install_file_publish(work,"resolver",&resolver))goto done;
 snprintf(ipath,sizeof(ipath),"%s/interfaces",work);snprintf(rpath,sizeof(rpath),"%s/resolver",work);snprintf(store,sizeof(store),"%s/store",work);
 env=*providers;env.interfaces_path=ipath;env.resolver_path=rpath;env.store_directory=store;env.service=0;
 for(i=0;i<2;i++){
  snprintf(path,sizeof(path),"etc/4vrs-network/%s",names[i]);
  if(install_file_read(root,path,&f))goto done;
  if(f.kind){existing++;snprintf(path,sizeof(path),"store/%s",names[i]);if(install_file_publish(work,path,&f))goto done;}
  install_file_free(&f);
 }
 if(existing==1){*stage="network-incomplete-store";goto done;}
 if(!existing){for(i=0;i<8;i++)bindings[i]=cfg.ports[i].bind_address;if(gateway_network_enroll_detailed(&env,bindings,stage))goto done;}
 if(gateway_network_boot_restore_detailed(&env,stage))goto done;
 *stage="network-plan";
 if(add(p,root,"etc/network/interfaces",0,&interfaces)||add(p,root,"etc/resolv.conf",0,&resolver)||add(p,root,"var/hda/4vrs/config/gateway.conf",1,&config))goto done;
 for(i=0;i<2;i++){snprintf(path,sizeof(path),"store/%s",names[i]);if(install_file_read(work,path,&f)||f.kind!=1)goto done;snprintf(path,sizeof(path),"etc/4vrs-network/%s",names[i]);if(add(p,root,path,0,&f))goto done;install_file_free(&f);}
 result=0;*stage="network-plan-ok";
 done:install_file_free(&interfaces);install_file_free(&resolver);install_file_free(&config);install_file_free(&f);return result;
}
