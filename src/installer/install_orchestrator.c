#define _GNU_SOURCE
#include "installer/install_orchestrator.h"
#include "installer/install_scripts.h"
#include "installer/install_apache.h"
#include "installer/install_digest.h"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
static int join(char*out,size_t capacity,const char*a,const char*b){size_t x=strlen(a),y=strlen(b);if(x+y+2>capacity)return -1;memcpy(out,a,x);out[x]='/';memcpy(out+x+1,b,y+1);return 0;}

/* Only the confirmed CF root is normalized; never follow a link or change
 * ownership, descendants, recovery-only invocations, or an unavailable CF. */
static int prepare_cf_root(install_context_t*c){
 char path[1024];struct stat before,opened,after;int fd,r=-1;
 if(install_path(c->root,"var/hda",path)||lstat(path,&before)||
    !S_ISDIR(before.st_mode)||before.st_uid!=geteuid())return -1;
 fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0)return -1;
 if(fstat(fd,&opened)||opened.st_dev!=before.st_dev||opened.st_ino!=before.st_ino||
    !S_ISDIR(opened.st_mode)||opened.st_uid!=geteuid())goto done;
 if((opened.st_mode&0022)&&(fchmod(fd,(opened.st_mode&07777)&~0022)||fsync(fd)))goto done;
 if(fstat(fd,&opened)||lstat(path,&after)||opened.st_dev!=after.st_dev||
    opened.st_ino!=after.st_ino||!S_ISDIR(after.st_mode)||after.st_uid!=geteuid()||
    (after.st_mode&0022))goto done;
 r=0;
 done:if(close(fd))r=-1;return r;
}
static int directory(install_context_t*c,const char*rel,unsigned int create){
 char path[1024],parent[1024],*slash;struct stat s;
 if(install_path(c->root,rel,path))return -1;
 if(!lstat(path,&s))return S_ISDIR(s.st_mode)&&s.st_uid==geteuid()&&!(s.st_mode&0022)?0:-1;
 if(errno!=ENOENT||!create||mkdir(path,0700))return -1;
 strcpy(parent,path);slash=strrchr(parent,'/');if(!slash)return -1;*slash=0;return install_directory_sync(parent);
}
int install_layout(install_context_t*c,unsigned int create){
 char path[1024],target[128];struct stat s;ssize_t n;unsigned int i;
 static const char*parents[]={"etc/4vrs-installer","etc/4vrs-network","var/hda/4vrs","var/hda/4vrs/bin","var/hda/4vrs/config","var/hda/4vrs/run","var/hda/4vrs/log"};
 c->stage="layout-alias";
 if(install_path(c->root,"etc/init.d",path)||lstat(path,&s))return -1;
 if(S_ISDIR(s.st_mode))strcpy(c->init_directory,"etc/init.d");
 else if(S_ISLNK(s.st_mode)){
  n=readlink(path,target,sizeof(target)-1);if(n<=0||n==(ssize_t)sizeof(target)-1)return -1;target[n]=0;
  if(strcmp(target,"rc.d/init.d")&&strcmp(target,"/etc/rc.d/init.d"))return -1;
  strcpy(c->init_directory,"etc/rc.d/init.d");
 }else return -1;
 if(directory(c,c->init_directory,0)||directory(c,"etc/rc.d/rcS.d",0)||directory(c,"etc/rc.d/rc3.d",0)||directory(c,"etc/rc.d/rc0.d",0)||directory(c,"etc/rc.d/rc6.d",0))return -1;
 c->stage="layout-storage";
 for(i=0;i<sizeof(parents)/sizeof(parents[0]);i++){
  if(i>=2&&c->platform->cf_available(c->platform_context)!=1){if(create)return -1;break;}
  if(i==2&&create&&prepare_cf_root(c))return -1;
  if(directory(c,parents[i],create))return -1;
 }
 if(snprintf(c->state_directory,sizeof(c->state_directory),"%s%s%s",c->root,!strcmp(c->root,"/")?"":"/",INSTALL_RECOVERY_DIRECTORY)>=(int)sizeof(c->state_directory))return -1;
 return 0;
}
static const char*const common_paths[]={
 "etc/network/interfaces","etc/resolv.conf","var/hda/4vrs/config/gateway.conf",
 "etc/4vrs-network/confirmed","etc/4vrs-network/good","etc/4vrs-network/gateway-network-recovery",
 "etc/4vrs-network/vendor-networking","etc/4vrs-network/vendor-networking-managed","etc/4vrs-network/enabled",
 "etc/4vrs-clock-managed","var/hda/4vrs/bin/4vrs-gateway",
 "etc/4vrs-installer/networking","etc/4vrs-installer/application",
 "etc/rc.d/rcS.d/S40networking","etc/rc.d/rc3.d/S90fourvrs-gateway",
 "etc/rc.d/rc0.d/K10fourvrs-gateway","etc/rc.d/rc6.d/K10fourvrs-gateway",
 "var/hda/4vrs/bin/4vrs-web","etc/rc.d/rcS.d/S21apache",
 "var/hda/4vrs/bin/4vrs-rng","var/hda/4vrs/bin/4vrs-kdf"
};
int install_allow_path(void*v,const char*path,unsigned int flags){
 install_context_t*c=v;unsigned int i,expected;char p[128];
 for(i=0;i<sizeof(common_paths)/sizeof(common_paths[0]);i++)if(!strcmp(path,common_paths[i])){
  expected=!strncmp(path,"var/hda/",8)?INSTALL_ON_CF:0;
  if(i>=13&&i<=16)expected=INSTALL_BOOT_GATE;
  return flags==expected?0:-1;
 }
 {static const char*names[]={"4vrs-gateway","ntpdate","ntpdate.d","halt"};
  for(i=0;i<4;i++){snprintf(p,sizeof(p),"%s/%s",c->init_directory,names[i]);if(!strcmp(path,p))return flags==(i?0:INSTALL_BOOT_GATE)?0:-1;}}
 return -1;
}
static int gate(void*v){install_context_t*c=v;install_file_t active;
 unsigned int i;
 active.kind=1;active.mode=0600;active.size=strlen(c->slot);active.data=(unsigned char*)c->slot;
 c->stage="bootstrap-active";
 if(install_file_publish(c->state_directory,"active",&active))return -1;
 if(c->boundary&&c->boundary(c->boundary_context,"active",0))return -1;
 c->stage="bootstrap-gates";
 for(i=0;i<c->plan.count;i++)if(c->plan.member[i].compact_flash&INSTALL_BOOT_GATE){
  if(install_file_publish(c->root,c->plan.member[i].path,&c->plan.member[i].after))return -1;
  if(c->boundary&&c->boundary(c->boundary_context,"bootstrap-gate",i))return -1;
 }
 return 0;
}
static int stop(void*v){install_context_t*c=v;int r;c->stage="services-stop";
 if(c->plan.count)c->restore_running=c->plan.was_running;
 else{install_plan_t saved;if(install_transaction_load(&c->transaction,&saved))return -1;c->restore_running=saved.was_running;install_plan_free(&saved);}
 c->transaction_services=1;r=c->platform->stop(c->platform_context);c->transaction_services=0;return r;
}
static int start(void*v,unsigned int old){install_context_t*c=v;c->stage=old?"services-restore":"services-start";if(old&&c->entry_mode)return c->restore_apache?c->restore_apache(c->restore_running&INSTALL_APACHE_RUNNING):0;return c->platform->start(c->platform_context,old);}
static int verify(void*v,unsigned int old){install_context_t*c=v;c->stage=old?"verify-restored":"verify-installed";if(old&&c->entry_mode)return 0;return c->platform->verify(c->platform_context,old);}
static int cf(void*v){install_context_t*c=v;return c->platform->cf_available(c->platform_context);}
static int boundary(void*v,const char*s,unsigned int i){install_context_t*c=v;
 if(!strcmp(s,"failed")&&!c->failure_stage){c->failure_stage=c->stage;c->failure_detail=c->detail;}
 return c->boundary?c->boundary(c->boundary_context,s,i):0;
}
static const install_transaction_ops_t transaction_ops={gate,stop,start,verify,cf,install_allow_path,boundary};
static void transaction(install_context_t*c){c->transaction.root=c->root;c->transaction.journal=c->journal_directory;c->transaction.ops=&transaction_ops;c->transaction.context=c;}
static int active(install_context_t*c){install_file_t f;int r;
 if(install_file_read(c->state_directory,"active",&f))return -1;
 if(!f.kind)return 0;
 r=f.kind==1&&f.size==5&&(!memcmp(f.data,"slot0",5)||!memcmp(f.data,"slot1",5));
 if(r){memcpy(c->slot,f.data,5);c->slot[5]=0;if(join(c->journal_directory,sizeof(c->journal_directory),c->state_directory,c->slot))return -1;transaction(c);}
 install_file_free(&f);return r?1:-1;
}
static int add(install_context_t*c,const char*p,const install_file_t*f){unsigned int flags=!strncmp(p,"var/hda/",8)?INSTALL_ON_CF:0;return install_plan_add(&c->plan,c->root,p,flags,f);}
static int named_script(install_context_t*c,const char*name,unsigned int halt){
 char path[128];install_file_t before,after;int r;
 c->stage=halt?"plan-clock-halt":!strcmp(name,"ntpdate")?"plan-clock-ntpdate":"plan-clock-ntpdate.d";c->detail=0;
 snprintf(path,sizeof(path),"%s/%s",c->init_directory,name);
 if(install_file_read(c->root,path,&before)){c->detail=5;return -1;}
 if(!before.kind&&!strcmp(name,"ntpdate.d"))return 0;
 r=install_clock_script(&before,halt,&after);if(r){c->detail=-r;install_file_free(&before);return -1;}
 r=add(c,path,&after);install_file_free(&before);install_file_free(&after);return r;
}
static int plan(install_context_t*c,const install_package_t*p){
 char path[128],work[1024];install_file_t source,derived,marker,link;unsigned int i;int r;
 c->stage="rng-schema";
 {install_file_t nv;unsigned char digest[32];install_digest_t h;int bad=0;char rngdir[1024];struct stat st;
  if(install_path(c->root,"var/hda/4vrs-rng",rngdir))return -1;
  if(lstat(rngdir,&st)){if(errno!=ENOENT)return -1;goto rng_schema_done;}
  if(!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0777)!=0700)return -1;
  if(install_file_read(c->root,"var/hda/4vrs-rng/state",&nv))return -1;
  if(nv.kind){size_t covered=nv.size==192?160:128;bad=nv.kind!=1||nv.mode!=0600||(nv.size!=160&&nv.size!=192);
   if(!bad){
    if(nv.size==160)bad=memcmp(nv.data,"4VRSNV01",8)!=0;
    else if(!memcmp(nv.data,"4VRSNV02",8))bad=memcmp(nv.data+128,"production-v1",13)!=0||nv.data[12]||nv.data[13]||nv.data[14]||!nv.data[15]||nv.data[15]>8;
    else if(!memcmp(nv.data,"4VRSNV03",8))bad=memcmp(nv.data+128,"production-autonomous-v1",24)!=0||!(nv.data[12]|nv.data[13]|nv.data[14]|nv.data[15]);
    else bad=1;
    install_digest_init(&h);install_digest_update(&h,nv.data,covered);install_digest_final(&h,digest);bad|=memcmp(digest,nv.data+covered,32)!=0;
   }
   {volatile unsigned char *q=nv.data;size_t n=nv.size;while(n--)*q++=0;}}
  install_file_free(&nv);if(bad)return -1;
 }
 rng_schema_done:
 c->stage="plan-network";if(join(work,sizeof(work),c->journal_directory,"work"))return -1;
 if(install_network_plan(c->root,work,c->network,&c->plan,&c->stage))return -1;
 c->stage="plan-vendor";snprintf(path,sizeof(path),"%s/networking",c->init_directory);
 if(install_file_read(c->root,path,&source))return -1;
 if(install_vendor_script(&source,&derived)){install_file_free(&source);return -1;}
 {install_file_t saved;
  if(install_file_read(c->root,"etc/4vrs-network/vendor-networking",&saved)){install_file_free(&source);install_file_free(&derived);return -1;}
  if(saved.kind&&!install_file_equal(&source,&saved)){install_file_free(&saved);install_file_free(&source);install_file_free(&derived);return -1;}
  install_file_free(&saved);
 }
 r=add(c,"etc/4vrs-network/vendor-networking",&source)||add(c,"etc/4vrs-network/vendor-networking-managed",&derived);
 install_file_free(&source);install_file_free(&derived);if(r)return -1;
 /* -f and -e guards agree only for our absent or regular owned marker.
  * Strict file reader rejects directories/devices/writable/foreign files;
  * explicitly reject symlinks/executable markers, preserve bounded descriptive
  * contents and valid metadata (r15 records its source revision here). */
 c->stage="plan-clock-marker";c->detail=0;
 if(install_file_read(c->root,"etc/4vrs-clock-managed",&marker)){c->detail=5;return -1;}
 if(marker.kind&&(marker.kind!=1||marker.size>256||(marker.mode&0111))){install_file_free(&marker);c->detail=6;return -1;}
 install_file_free(&marker);
 if(named_script(c,"ntpdate",0)||named_script(c,"ntpdate.d",0)||named_script(c,"halt",1))return -1;
 c->stage="plan-components";
 c->stage="apache-startup-plan";if(install_apache_plan(c->root,c->init_directory,&c->plan))return -1;
 if(add(c,"var/hda/4vrs/bin/4vrs-gateway",&p->payload[1])||add(c,"etc/4vrs-network/gateway-network-recovery",&p->payload[1])||add(c,"etc/4vrs-installer/application",&p->payload[2])||add(c,"etc/4vrs-installer/networking",&p->payload[3])||add(c,"var/hda/4vrs/bin/4vrs-web",&p->payload[4])||add(c,"var/hda/4vrs/bin/4vrs-rng",&p->payload[5])||add(c,"var/hda/4vrs/bin/4vrs-kdf",&p->payload[6]))return -1;
 marker.kind=1;marker.mode=0600;marker.size=0;marker.data=0;
 if(add(c,"etc/4vrs-network/enabled",&marker))return -1;
 if(install_file_read(c->root,"etc/4vrs-clock-managed",&source))return -1;
 r=add(c,"etc/4vrs-clock-managed",source.kind?&source:&marker);install_file_free(&source);if(r)return -1;
 c->stage="plan-boot";
 if(install_bootstrap_add_gate(&c->plan,c->root,"etc/rc.d/rcS.d/S40networking",0))return -1;
 snprintf(path,sizeof(path),"%s/4vrs-gateway",c->init_directory);if(install_bootstrap_add_gate(&c->plan,c->root,path,1))return -1;
 /* Known vendor link, exact reviewed product script, or our exact gate.
  * An unknown boot entry is never overwritten as a guessed migration. */
 for(i=0;i<c->plan.count;i++)if(c->plan.member[i].compact_flash&INSTALL_BOOT_GATE){
  install_member_t*m=&c->plan.member[i];unsigned int app=strcmp(m->path,"etc/rc.d/rcS.d/S40networking")!=0;
  if(install_file_equal(&m->before,&m->after)||install_file_equal(&m->before,&p->payload[app?2:3]))continue;
  if(app&&!m->before.kind)continue;
  if(!app&&m->before.kind==2&&(!strcmp((char*)m->before.data,"../init.d/networking")||!strcmp((char*)m->before.data,"/etc/init.d/networking")))continue;
  return -1;
 }
 link.kind=2;link.mode=0777;link.data=(unsigned char*)"/etc/init.d/4vrs-gateway";link.size=strlen((char*)link.data);
 for(i=14;i<=16;i++){
  if(install_file_read(c->root,common_paths[i],&source))return -1;
  if(source.kind&&!(source.kind==2&&(!strcmp((char*)source.data,"/etc/init.d/4vrs-gateway")||!strcmp((char*)source.data,"../init.d/4vrs-gateway")))){install_file_free(&source);return -1;}
  r=install_plan_add(&c->plan,c->root,common_paths[i],INSTALL_BOOT_GATE,source.kind?&source:&link);install_file_free(&source);if(r)return -1;
 }
 return 0;
}
void install_context_release(install_context_t*c){install_plan_free(&c->plan);}
int install_recover_only(install_context_t*c){
 int r;c->stage="recover-layout";
 if(install_layout(c,0))return INSTALL_RECOVERY_REQUIRED;
 r=active(c);if(r<0)return INSTALL_RECOVERY_REQUIRED;
 if(!r)return INSTALL_UNCHANGED;
 c->stage="recover-only";return install_transaction_recover(&c->transaction);
}
int install_recover_entry(install_context_t*c,unsigned int application,const char*action){
 int r;c->entry_mode=1;c->stage="entry-layout";if(install_layout(c,0))return INSTALL_RECOVERY_REQUIRED;
 r=active(c);if(r!=1)return INSTALL_RECOVERY_REQUIRED;
 c->stage="entry-recover";r=install_transaction_recover(&c->transaction);
 if(r==INSTALL_WAIT_CF){if(application)return r;return c->platform->entry(c->platform_context,0,action,1)?INSTALL_RECOVERY_REQUIRED:r;}
 if(r!=INSTALL_COMPLETED&&r!=INSTALL_ROLLED_BACK)return INSTALL_RECOVERY_REQUIRED;
 if(application&&c->platform->cf_available(c->platform_context)!=1)return INSTALL_WAIT_CF;
 return c->platform->entry(c->platform_context,application,action,0)?INSTALL_RECOVERY_REQUIRED:0;
}
/* Only an inactive slot may be recycled. The last terminal transaction is
 * kept in the other slot throughout preparation. Unknown files are retained
 * and block reuse instead of being recursively erased. */
static int inventory(const char*root,const char*const*names,unsigned int n,const char*directory_name){
 DIR*d=opendir(root);struct dirent*e;unsigned int seen=0,i;install_file_t empty={0,0,0,0};
 if(!d)return -1;
 for(;;){int known=0,temporary=0;errno=0;e=readdir(d);if(!e){if(errno){closedir(d);return -1;}break;}
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  if(++seen>128){closedir(d);return -1;}
  if(directory_name&&!strcmp(e->d_name,directory_name))continue;
  for(i=0;i<n;i++){
   size_t z=strlen(names[i]);
   if(!strcmp(e->d_name,names[i])){known=1;break;}
   if(!strncmp(e->d_name,names[i],z)&&!strncmp(e->d_name+z,".install-",9)&&strlen(e->d_name+z+9)==6){
    const char*s=e->d_name+z+9;unsigned int j;
    for(j=0;j<6;j++)if(!((s[j]>='a'&&s[j]<='z')||(s[j]>='A'&&s[j]<='Z')||(s[j]>='0'&&s[j]<='9')))break;
    if(j==6){known=temporary=1;break;}
   }
  }
  if(!known||(temporary&&install_file_publish(root,e->d_name,&empty))){closedir(d);return -1;}
 }
 closedir(d);return 0;
}
static int retire_slot(install_context_t*c){
 static const char*files[]={"completed","rolled-back","cutover","journal"};
 install_file_t empty={0,0,0,0};install_plan_t previous;unsigned int i;int r;
 r=install_transaction_status(&c->transaction);
 if(r==INSTALL_RECOVERY_REQUIRED)return -1;
 if(inventory(c->journal_directory,files,4,"work"))return -1;
 if(r==INSTALL_WAIT_CF){
  /* Interrupted before active publication: no cutover could have occurred.
   * Accept disposal only while EVERY before-image still matches. */
  if(install_transaction_load(&c->transaction,&previous))return -1;
  for(i=0;i<previous.count;i++){install_file_t f;int same;
   if(install_file_read(c->root,previous.member[i].path,&f)){install_plan_free(&previous);return -1;}
   same=install_file_equal(&f,&previous.member[i].before);install_file_free(&f);
   if(!same){install_plan_free(&previous);return -1;}
  }
  install_plan_free(&previous);
 }
 /* Remove journal first: an interrupted retirement then has no transaction.
  * Old markers are removed before a new journal can be published. */
 for(i=4;i>0;i--)if(install_file_publish(c->journal_directory,files[i-1],&empty))return -1;
 return 0;
}
static int clear_work(install_context_t*c){
 static const char*names[]={"work/interfaces","work/resolver","work/store/confirmed","work/store/good","work/store/candidate","work/store/commit.guard","work/store/write.tmp","work/store/lock"};
 install_file_t empty={0,0,0,0};unsigned int i;
 {char path[1024];static const char*work[]={"interfaces","resolver"};static const char*store[]={"confirmed","good","candidate","commit.guard","write.tmp","lock"};
  if(join(path,sizeof(path),c->journal_directory,"work")||inventory(path,work,2,"store"))return -1;
  if(join(path,sizeof(path),c->journal_directory,"work/store")||inventory(path,store,6,0))return -1;
 }
 for(i=0;i<sizeof(names)/sizeof(names[0]);i++)if(install_file_publish(c->journal_directory,names[i],&empty))return -1;
 return 0;
}
int install_orchestrate(install_context_t*c,const install_package_t*p){
 char rel[128];unsigned int running;int r,i;
 c->stage="detect";if(c->platform->detect(c->platform_context))return INSTALL_REFUSED;
 if(install_layout(c,1))return INSTALL_REFUSED;
 r=active(c);if(r<0)return INSTALL_RECOVERY_REQUIRED;
 if(r){r=install_transaction_recover(&c->transaction);if(r!=INSTALL_COMPLETED&&r!=INSTALL_ROLLED_BACK)return r;strcpy(c->slot,!strcmp(c->slot,"slot0")?"slot1":"slot0");}
 else strcpy(c->slot,"slot0");
 snprintf(rel,sizeof(rel),"%s/%s",INSTALL_RECOVERY_DIRECTORY,c->slot);
 if(directory(c,rel,1))return INSTALL_REFUSED;
 if(join(c->journal_directory,sizeof(c->journal_directory),c->state_directory,c->slot))return INSTALL_REFUSED;
 transaction(c);
 if(retire_slot(c))return INSTALL_RECOVERY_REQUIRED;
 snprintf(rel,sizeof(rel),"%s/%s/work",INSTALL_RECOVERY_DIRECTORY,c->slot);if(directory(c,rel,1))return INSTALL_REFUSED;
 snprintf(rel,sizeof(rel),"%s/%s/work/store",INSTALL_RECOVERY_DIRECTORY,c->slot);if(directory(c,rel,1))return INSTALL_REFUSED;
 if(clear_work(c))return INSTALL_REFUSED;
 c->stage="inspect-services";if(c->platform->inspect(c->platform_context,&running))return INSTALL_REFUSED;
 if(plan(c,p))return INSTALL_REFUSED;
 c->plan.was_running=running;
 if(c->platform->capacity){
  size_t early=p->payload[0].size*2U+65536U,compact=65536U;unsigned int j;
  /* Journal contains both generations; reserve publication temporaries too.
   * Space checks are preflight, never a replacement for fsync/error checks. */
  for(j=0;j<c->plan.count;j++){
   install_member_t*m=&c->plan.member[j];early+=m->before.size+m->after.size+512U;
   if(m->compact_flash&INSTALL_ON_CF)compact+=m->after.size;
   else early+=m->after.size;
  }
  c->stage="storage-capacity";
  if(c->platform->capacity(c->platform_context,early,compact))return INSTALL_REFUSED;
 }
 c->decision="plan-equal";c->decision_count=c->decision_mask=0;c->decision_path[0]=0;
 for(i=0;i<(int)c->plan.count;i++){
  install_member_t*m=&c->plan.member[i];
  if(install_file_equal(&m->before,&m->after))continue;
  if(!c->decision_count){
   snprintf(c->decision_path,sizeof(c->decision_path),"%s",m->path);
   c->decision_mask=(m->before.kind!=m->after.kind?1U:0U)|(m->before.mode!=m->after.mode?2U:0U)|(m->before.size!=m->after.size?4U:0U);
   if(m->before.size==m->after.size&&m->before.size&&memcmp(m->before.data,m->after.data,m->before.size))c->decision_mask|=8U;
  }
  c->decision_count++;
 }
 r=-1;c->decision_health="not-run-plan-differs";
 if(!c->decision_count){c->verify_reason=0;r=c->platform->verify(c->platform_context,0);c->decision_health=r?(c->verify_reason?c->verify_reason:"unspecified"):"healthy";}
 c->decision=c->decision_count?"repair-plan-differs":r?"repair-platform-unhealthy":"no-op";
 if(!c->decision_count&&!r){
  install_file_t recovery;int same;
  if(install_file_read(c->root,INSTALL_RECOVERY_DIRECTORY "/recovery",&recovery))return INSTALL_REFUSED;
  same=install_file_equal(&recovery,&p->payload[0]);install_file_free(&recovery);
  if(same){c->stage="already-installed";return INSTALL_UNCHANGED;}
  c->decision="repair-recovery-only";c->stage="repair-recovery-executable";
  return install_bootstrap_prepare(c->root,&p->payload[0])?INSTALL_REFUSED:INSTALL_COMPLETED;
 }
 c->stage="bootstrap-executable";if(install_bootstrap_prepare(c->root,&p->payload[0]))return INSTALL_REFUSED;
 if(c->boundary&&c->boundary(c->boundary_context,"bootstrap-executable",0))return INSTALL_RECOVERY_REQUIRED;
 return install_transaction_run(&c->transaction,&c->plan);
}
