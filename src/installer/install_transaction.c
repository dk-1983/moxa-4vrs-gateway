#include "installer/install_transaction.h"
#include "installer/install_digest.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define JOURNAL_LIMIT INSTALL_FILE_LIMIT
static int valid_file(const install_file_t *);
static int point(install_transaction_t*t,const char*s,unsigned int i){return t->ops->boundary?t->ops->boundary(t->context,s,i):0;}
void install_plan_free(install_plan_t*p){unsigned int i;for(i=0;i<p->count;i++){install_file_free(&p->member[i].before);install_file_free(&p->member[i].after);}memset(p,0,sizeof(*p));}
static int copy_file(install_file_t*d,const install_file_t*s){*d=*s;d->data=0;if(s->size){d->data=malloc(s->size+1);if(!d->data)return -1;memcpy(d->data,s->data,s->size);d->data[s->size]=0;}return 0;}
int install_plan_add(install_plan_t*p,const char*root,const char*path,unsigned int cf,const install_file_t*after){
 unsigned int i;install_member_t*m;
 if(!p||!after||!valid_file(after)||!path||strlen(path)>INSTALL_PATH_LIMIT||p->count>=INSTALL_MEMBERS||cf>3)return -1;
 for(i=0;i<p->count;i++)if(!strcmp(p->member[i].path,path))return -1;
 m=&p->member[p->count];memset(m,0,sizeof(*m));strcpy(m->path,path);m->compact_flash=cf;
 if(install_file_read(root,path,&m->before)||copy_file(&m->after,after)){install_file_free(&m->before);return -1;}
 p->count++;return 0;
}
static void put(unsigned char*p,unsigned int v){p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
static unsigned int get(const unsigned char*p){return((unsigned int)p[0]<<24)|((unsigned int)p[1]<<16)|((unsigned int)p[2]<<8)|p[3];}
static int valid_file(const install_file_t*f){return f->kind<=2&&f->size<=INSTALL_FILE_LIMIT&&!(f->mode&~0777U)&&(!f->size||f->data)&&
 (f->kind||(!f->size&&!f->mode))&&(f->kind!=1||!(f->mode&0022))&&(f->kind!=2||(f->size&&f->size<=INSTALL_PATH_LIMIT&&!memchr(f->data,0,f->size)));}
static int encode(const install_plan_t*p,install_file_t*out){
 size_t n=16,at=16,z;unsigned int i,j;char digest[65];
 if(!p->count||p->count>INSTALL_MEMBERS||p->was_running>1)return -1;
 for(i=0;i<p->count;i++){
  const install_member_t*m=&p->member[i];z=strlen(m->path);if(!z||z>INSTALL_PATH_LIMIT||!valid_file(&m->before)||!valid_file(&m->after))return -1;
  n+=8+z+24+m->before.size+m->after.size;if(n>JOURNAL_LIMIT-64)return -1;
 }
 memset(out,0,sizeof(*out));out->data=calloc(1,n+65);if(!out->data)return -1;out->kind=1;out->mode=0600;out->size=n+64;
 memcpy(out->data,"4VIJ0001",8);put(out->data+8,p->count);put(out->data+12,p->was_running);
 for(i=0;i<p->count;i++){
  const install_member_t*m=&p->member[i];z=strlen(m->path);put(out->data+at,(unsigned int)z);put(out->data+at+4,m->compact_flash);at+=8;memcpy(out->data+at,m->path,z);at+=z;
  for(j=0;j<2;j++){const install_file_t*f=j?&m->after:&m->before;put(out->data+at,f->kind);put(out->data+at+4,f->mode);put(out->data+at+8,(unsigned int)f->size);at+=12;if(f->size)memcpy(out->data+at,f->data,f->size);at+=f->size;}
 }
 install_digest_hex(out->data,n,digest);memcpy(out->data+n,digest,64);return 0;
}
static int decode(install_transaction_t*t,const install_file_t*in,install_plan_t*p){
 size_t at=16,end,z;unsigned int i,j;char digest[65];
 memset(p,0,sizeof(*p));if(in->kind!=1||in->size<80||in->size>JOURNAL_LIMIT||memcmp(in->data,"4VIJ0001",8))return -1;
 end=in->size-64;install_digest_hex(in->data,end,digest);if(memcmp(digest,in->data+end,64))return -1;
 p->count=get(in->data+8);p->was_running=get(in->data+12);if(!p->count||p->count>INSTALL_MEMBERS||p->was_running>1){p->count=0;return -1;}
 for(i=0;i<p->count;i++){
  install_member_t*m=&p->member[i];if(end-at<8)goto fail;z=get(in->data+at);m->compact_flash=get(in->data+at+4);at+=8;
  if(!z||z>INSTALL_PATH_LIMIT||z>end-at||m->compact_flash>3||memchr(in->data+at,0,z))goto fail;
  memcpy(m->path,in->data+at,z);m->path[z]=0;at+=z;
  if(t->ops->allow_path(t->context,m->path,m->compact_flash))goto fail;
  for(j=0;j<i;j++)if(!strcmp(m->path,p->member[j].path))goto fail;
  for(j=0;j<2;j++){
   install_file_t*f=j?&m->after:&m->before;if(end-at<12)goto fail;
   f->kind=get(in->data+at);f->mode=get(in->data+at+4);f->size=get(in->data+at+8);at+=12;
   if(f->size>end-at||f->size>INSTALL_FILE_LIMIT)goto fail;
   if(f->size){f->data=malloc(f->size+1);if(!f->data)goto fail;memcpy(f->data,in->data+at,f->size);f->data[f->size]=0;}
   if(!valid_file(f))goto fail;
   at+=f->size;
  }
 }
 if(at!=end)goto fail;
 return 0;
 fail:install_plan_free(p);return -1;
}
static int marker(install_transaction_t*t,const char*name,const install_file_t*j){
 char digest[65];install_file_t m;install_digest_hex(j->data,j->size,digest);m.kind=1;m.mode=0600;m.size=64;m.data=(unsigned char*)digest;return install_file_publish(t->journal,name,&m);
}
static int marked(install_transaction_t*t,const char*name,const install_file_t*j){
 install_file_t m;char digest[65];int r;
 if(install_file_read(t->journal,name,&m))return -1;
 if(!m.kind)return 0;
 install_digest_hex(j->data,j->size,digest);r=m.kind==1&&m.size==64&&!memcmp(m.data,digest,64)?1:-1;install_file_free(&m);return r;
}
static int matches(install_transaction_t*t,const install_plan_t*p,unsigned int old){
 unsigned int i;install_file_t f;int r;
 for(i=0;i<p->count;i++){if(install_file_read(t->root,p->member[i].path,&f))return -1;r=install_file_equal(&f,old?&p->member[i].before:&p->member[i].after);install_file_free(&f);if(!r)return -1;}return 0;
}
static int matches_after_gate(install_transaction_t*t,const install_plan_t*p){
 unsigned int i;install_file_t f;int r;
 for(i=0;i<p->count;i++){
  const install_member_t*m=&p->member[i];
  if(install_file_read(t->root,m->path,&f))return -1;
  r=install_file_equal(&f,(m->compact_flash&INSTALL_BOOT_GATE)?&m->after:&m->before);
  install_file_free(&f);if(!r)return -1;
 }
 return 0;
}
static int restore(install_transaction_t*t,const install_plan_t*p,const install_file_t*j){
 unsigned int i,pass;int cf=t->ops->cf_available(t->context),pending=0,cutover=marked(t,"cutover",j);
 if(cf<0||cutover<0||t->ops->stop(t->context))return INSTALL_RECOVERY_REQUIRED;
 for(pass=0;pass<2;pass++)for(i=0;i<p->count;i++){
  const install_member_t*m=&p->member[i];
  unsigned int is_gate=(m->compact_flash&INSTALL_BOOT_GATE)?1U:0U;
  if(is_gate!=pass)continue;
  /* Before durable cutover only gates were changed by the installer.
   * Preserve an operator/application edit that caused snapshot recheck to
   * refuse installation; do not restore stale before-images over it. */
  if(!cutover&&!is_gate)continue;
  /* Restore gates LAST. Keep them installed while CF is unavailable: a
   * later app start must not bypass the unfinished transaction. */
  if(!cf&&cutover&&((m->compact_flash&INSTALL_ON_CF)||is_gate)){pending=1;continue;}
  if(install_file_equal(&m->before,&m->after))continue;
  if(install_file_publish(t->root,m->path,&m->before)||point(t,"restore",i))return INSTALL_RECOVERY_REQUIRED;
 }
 /* Early boot may restore the entire /etc set while CF is absent. The gate
  * must stay installed: no app activation or terminal marker is permitted. */
 if(pending)return INSTALL_WAIT_CF;
 if((cutover&&matches(t,p,1))||(p->was_running&&t->ops->start(t->context,1))||t->ops->verify(t->context,1)||marker(t,"rolled-back",j))return INSTALL_RECOVERY_REQUIRED;
 return INSTALL_ROLLED_BACK;
}
int install_transaction_load(install_transaction_t*t,install_plan_t*p){
 install_file_t j;int r;
 if(install_file_read(t->journal,"journal",&j))return -1;
 if(!j.kind)return -1;
 r=decode(t,&j,p);install_file_free(&j);return r;
}
int install_transaction_status(install_transaction_t*t){
 install_file_t j;install_plan_t p;int a,b;
 if(install_file_read(t->journal,"journal",&j))return INSTALL_RECOVERY_REQUIRED;
 if(!j.kind)return INSTALL_UNCHANGED;
 if(decode(t,&j,&p)){install_file_free(&j);return INSTALL_RECOVERY_REQUIRED;}
 install_plan_free(&p);a=marked(t,"completed",&j);b=marked(t,"rolled-back",&j);install_file_free(&j);
 if(a<0||b<0||(a&&b))return INSTALL_RECOVERY_REQUIRED;
 return a?INSTALL_COMPLETED:b?INSTALL_ROLLED_BACK:INSTALL_WAIT_CF;
}
int install_transaction_recover(install_transaction_t*t){
 install_file_t j;install_plan_t p;int r,c;
 if(install_file_read(t->journal,"journal",&j))return INSTALL_RECOVERY_REQUIRED;
 if(!j.kind)return INSTALL_UNCHANGED;
 if(decode(t,&j,&p)){install_file_free(&j);return INSTALL_RECOVERY_REQUIRED;}
 c=marked(t,"completed",&j);r=marked(t,"rolled-back",&j);
 if(c<0||r<0||(c&&r))r=INSTALL_RECOVERY_REQUIRED;
 /* A terminal transaction must not freeze subsequently edited product
  * settings. Whole-set equality is checked before terminal publication;
  * future no-op/repair inspection belongs to the current installation plan. */
 else if(c)r=install_directory_sync(t->journal)?INSTALL_RECOVERY_REQUIRED:INSTALL_COMPLETED;
 else if(r)r=install_directory_sync(t->journal)?INSTALL_RECOVERY_REQUIRED:INSTALL_ROLLED_BACK;
 else r=restore(t,&p,&j);
 install_plan_free(&p);install_file_free(&j);return r;
}
int install_transaction_run(install_transaction_t*t,install_plan_t*p){
 install_file_t j,existing;unsigned int i;int r,changed=0;
 if(!t||!p||!p->count||p->count>INSTALL_MEMBERS||!t->ops||!t->ops->gate||!t->ops->stop||!t->ops->start||!t->ops->verify||!t->ops->cf_available||!t->ops->allow_path)return INSTALL_REFUSED;
 if(install_file_read(t->journal,"journal",&existing))return INSTALL_REFUSED;
 if(existing.kind){install_file_free(&existing);return INSTALL_REFUSED;}
 for(i=0;i<p->count;i++){if(t->ops->allow_path(t->context,p->member[i].path,p->member[i].compact_flash))return INSTALL_REFUSED;if(!install_file_equal(&p->member[i].before,&p->member[i].after))changed=1;}
 /* An unhealthy byte-identical installation is a service repair. It still
  * needs a journal and boot protection before any lifecycle action. */
 if(!changed&&!t->ops->verify(t->context,0))return INSTALL_UNCHANGED;
 if(t->ops->cf_available(t->context)!=1||matches(t,p,1)||encode(p,&j))return INSTALL_REFUSED;
 /* Journal publication is before gate publication and before any service
  * stop/installed-set writes. Bootstrap failures therefore retain old files. */
 if(install_file_publish(t->journal,"journal",&j)||point(t,"journal",0)||t->ops->gate(t->context)||point(t,"gate",0)){install_file_free(&j);return INSTALL_RECOVERY_REQUIRED;}
 if(matches_after_gate(t,p)||t->ops->stop(t->context)||point(t,"stopped",0))goto rollback;
 if(matches_after_gate(t,p))goto rollback;
 if(marker(t,"cutover",&j)||point(t,"cutover",0))goto rollback;
 for(i=0;i<p->count;i++){
  if(install_file_equal(&p->member[i].before,&p->member[i].after))continue;
  if(install_file_publish(t->root,p->member[i].path,&p->member[i].after)||point(t,"publish",i))goto rollback;
 }
 if(matches(t,p,0)||t->ops->start(t->context,0)||point(t,"activated",0)||t->ops->verify(t->context,0)||point(t,"verified",0))goto rollback;
 /* If publication/fsync of the terminal marker fails, do not race rollback
  * against a possibly durable commit. Recovery resolves the marker first. */
 r=marker(t,"completed",&j)?INSTALL_RECOVERY_REQUIRED:INSTALL_COMPLETED;
 if(point(t,"completed",0))r=INSTALL_RECOVERY_REQUIRED;
 install_file_free(&j);return r;
 rollback:(void)point(t,"failed",0);r=restore(t,p,&j);install_file_free(&j);return r;
}
