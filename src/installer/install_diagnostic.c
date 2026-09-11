#define _GNU_SOURCE
#include "installer/install_diagnostic.h"
#include "installer/install_orchestrator.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int allowed(void*v,const char*p,unsigned int flags){
 install_context_t*c=v;strcpy(c->init_directory,"etc/init.d");
 if(!install_allow_path(c,p,flags))return 0;
 strcpy(c->init_directory,"etc/rc.d/init.d");return install_allow_path(c,p,flags);
}
/* Reads retained before/after journals under the existing installer lock.
 * No creation, recovery, service action, CF access, file contents or digests. */
int install_decision_journal(void){
 int fd,result=0;unsigned int slot,i;struct stat st;install_context_t c;
 install_transaction_ops_t ops;memset(&c,0,sizeof(c));memset(&ops,0,sizeof(ops));ops.allow_path=allowed;
 if(geteuid()!=0)return 1;
 fd=open("/etc/4vrs-installer/install.lock",O_RDONLY|O_NOFOLLOW);
 if(fd<0)return 1;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||(st.st_mode&0077)||flock(fd,LOCK_SH|LOCK_NB)){close(fd);return 1;}
 for(slot=0;slot<2;slot++){
  char dir[80],path[96];install_transaction_t t;install_plan_t p;unsigned int differences=0;
  snprintf(dir,sizeof(dir),"/etc/4vrs-installer/slot%u",slot);snprintf(path,sizeof(path),"%s/journal",dir);
  if(lstat(path,&st)){if(errno==ENOENT){printf("slot=%u journal=absent\n",slot);continue;}result=1;break;}
  memset(&t,0,sizeof(t));t.journal=dir;t.context=&c;t.ops=&ops;
  if(install_transaction_load(&t,&p)){result=1;break;}
  for(i=0;i<p.count;i++){
   install_member_t*m=&p.member[i];if(install_file_equal(&m->before,&m->after))continue;
   differences++;
   printf("slot=%u path=%s before_kind=%u after_kind=%u before_mode=%03o after_mode=%03o before_size=%lu after_size=%lu content_changed=%u\n",slot,m->path,m->before.kind,m->after.kind,m->before.mode,m->after.mode,(unsigned long)m->before.size,(unsigned long)m->after.size,(unsigned int)(m->before.size!=m->after.size||(m->before.size&&memcmp(m->before.data,m->after.data,m->before.size))));
  }
  printf("slot=%u journal=valid differences=%u was_running=%u\n",slot,differences,p.was_running);
  install_plan_free(&p);
 }
 close(fd);return result;
}
