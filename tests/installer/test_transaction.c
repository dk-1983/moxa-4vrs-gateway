#define _GNU_SOURCE
#include "installer/install_transaction.h"
#include "installer/install_digest.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned int checks;
static unsigned int sync_count,sync_fail;
int __real_fsync(int);
int __wrap_fsync(int fd){sync_count++;if(sync_fail&&sync_count==sync_fail)return -1;return __real_fsync(fd);}
typedef struct fixture{char root[1024],journal[1024];const char*crash;unsigned int index;int cf,fail_stop,fail_start,fail_verify,starts,stops,gated; } fixture_t;
static install_file_t file(const char*s,unsigned int mode){install_file_t f;f.kind=1;f.mode=mode;f.size=strlen(s);f.data=(unsigned char*)s;return f;}
static int gate(void*v){fixture_t*f=v;install_file_t b=file("durable gate",0700);f->gated=1;return install_file_publish(f->root,"gate",&b);}
static int stop(void*v){fixture_t*f=v;f->stops++;return f->fail_stop?-1:0;}
static int start(void*v,unsigned int old){fixture_t*f=v;f->starts++;return !old&&f->fail_start?-1:0;}
static int verify(void*v,unsigned int old){fixture_t*f=v;return !old&&f->fail_verify?-1:0;}
static int cf(void*v){return((fixture_t*)v)->cf;}
static int allow(void*v,const char*p,unsigned int c){(void)v;return ((!strcmp(p,"helper")&&!c)||(!strcmp(p,"app")&&c)||(!strcmp(p,"startup")&&!c)||(!strcmp(p,"marker")&&!c))?0:-1;}
static int boundary(void*v,const char*s,unsigned int i){fixture_t*f=v;if(f->crash&&!strcmp(f->crash,s)&&f->index==i)_exit(77);return 0;}
static const install_transaction_ops_t ops={gate,stop,start,verify,cf,allow,boundary};
static void setup(fixture_t*f,install_plan_t*p,const char*base){
 char path[1100];install_file_t a=file("old helper\n",0700),b=file("old application\n",0755),link;
 memset(f,0,sizeof(*f));memset(p,0,sizeof(*p));f->cf=1;
 CHECK(snprintf(f->root,sizeof(f->root),"%s/case-XXXXXX",base)>0);CHECK(mkdtemp(f->root)!=0);
 snprintf(path,sizeof(path),"%s/journal",f->root);CHECK(mkdir(path,0700)==0);CHECK(strlen(path)<sizeof(f->journal));strcpy(f->journal,path);
 CHECK(install_file_publish(f->root,"helper",&a)==0);CHECK(install_file_publish(f->root,"app",&b)==0);
 link.kind=2;link.mode=0777;link.data=(unsigned char*)"../vendor/start";link.size=strlen((char*)link.data);CHECK(install_file_publish(f->root,"startup",&link)==0);
 a=file("new helper\n",0755);CHECK(install_plan_add(p,f->root,"helper",0,&a)==0);
 a=file("new application\n",0700);CHECK(install_plan_add(p,f->root,"app",1,&a)==0);
 a=file("#!/bin/sh\nnew startup\n",0755);CHECK(install_plan_add(p,f->root,"startup",0,&a)==0);
 a=file("",0600);CHECK(install_plan_add(p,f->root,"marker",0,&a)==0);p->was_running=1;
}
static void expect(fixture_t*f,install_plan_t*p,unsigned int old,unsigned int partial){unsigned int i;install_file_t actual;for(i=0;i<p->count;i++){if(partial&&p->member[i].compact_flash)continue;CHECK(install_file_read(f->root,p->member[i].path,&actual)==0);CHECK(install_file_equal(&actual,old?&p->member[i].before:&p->member[i].after));install_file_free(&actual);}}
static void crash_run(fixture_t*f,install_plan_t*p){int s;pid_t child=fork();CHECK(child>=0);if(!child){install_transaction_t t={f->root,f->journal,&ops,f};(void)install_transaction_run(&t,p);_exit(1);}CHECK(waitpid(child,&s,0)==child);CHECK(WIFEXITED(s)&&WEXITSTATUS(s)==77);}
int main(int argc,char**argv){
 const char*points[]={"journal","gate","stopped","publish","publish","publish","publish","activated","verified","completed"};
 unsigned int indexes[]={0,0,0,0,1,2,3,0,0,0},i;fixture_t f;install_plan_t p;install_transaction_t t;install_file_t b;char digest[65],path[1100];int fd;
 CHECK(argc==2);
 install_digest_hex("",0,digest);CHECK(!strcmp(digest,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
 install_digest_hex("abc",3,digest);CHECK(!strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
 install_digest_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",56,digest);CHECK(!strcmp(digest,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
 for(i=0;i<10;i++){
  setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;t.ops=&ops;t.context=&f;
  f.crash=points[i];f.index=indexes[i];crash_run(&f,&p);f.crash=0;
  CHECK(install_transaction_recover(&t)==(i==9?INSTALL_COMPLETED:INSTALL_ROLLED_BACK));expect(&f,&p,i!=9,0);
  CHECK(install_transaction_recover(&t)==(i==9?INSTALL_COMPLETED:INSTALL_ROLLED_BACK));expect(&f,&p,i!=9,0);install_plan_free(&p);
 }
 /* Missing CF restores /etc but cannot mark terminal or start the app. */
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;t.context=&f;f.crash="activated";crash_run(&f,&p);f.crash=0;f.cf=0;
 CHECK(install_transaction_recover(&t)==INSTALL_WAIT_CF);expect(&f,&p,1,1);CHECK(f.starts==0);
 CHECK(install_transaction_recover(&t)==INSTALL_WAIT_CF);f.cf=1;CHECK(install_transaction_recover(&t)==INSTALL_ROLLED_BACK);expect(&f,&p,1,0);install_plan_free(&p);
 /* Failed activation restores modes and original symlink; absent marker stays absent. */
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;f.fail_start=1;
 CHECK(install_transaction_run(&t,&p)==INSTALL_ROLLED_BACK);expect(&f,&p,1,0);install_plan_free(&p);
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;f.fail_verify=1;
 CHECK(install_transaction_run(&t,&p)==INSTALL_ROLLED_BACK);expect(&f,&p,1,0);install_plan_free(&p);
 /* No publication if an identified process cannot be stopped. */
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;f.fail_stop=1;
 CHECK(install_transaction_run(&t,&p)==INSTALL_RECOVERY_REQUIRED);expect(&f,&p,1,0);f.fail_stop=0;CHECK(install_transaction_recover(&t)==INSTALL_ROLLED_BACK);install_plan_free(&p);
 /* Corruption is neither no-op nor permission to restore untrusted paths. */
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;f.crash="activated";crash_run(&f,&p);f.crash=0;
 snprintf(path,sizeof(path),"%s/journal",f.journal);fd=open(path,O_WRONLY);CHECK(fd>=0);CHECK(write(fd,"X",1)==1);CHECK(close(fd)==0);
 CHECK(install_transaction_recover(&t)==INSTALL_RECOVERY_REQUIRED);expect(&f,&p,0,0);CHECK(f.stops==0);install_plan_free(&p);
 /* Root/parent traversal, symlink/hardlink input and changed preimage refusal. */
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;
 CHECK(install_file_read(f.root,"../outside",&b)<0);CHECK(install_file_read(f.root,"startup/child",&b)<0);
 snprintf(path,sizeof(path),"%s/alias",f.root);{char app[1100];snprintf(app,sizeof(app),"%s/app",f.root);CHECK(link(app,path)==0);}
 CHECK(install_file_read(f.root,"app",&b)<0);CHECK(unlink(path)==0);
 b=file("external changed app",0700);CHECK(install_file_publish(f.root,"app",&b)==0);CHECK(install_transaction_run(&t,&p)==INSTALL_REFUSED);CHECK(!f.gated&&!f.stops);install_plan_free(&p);
 /* Every file/directory durability failure in a complete cutover. Recovery
  * must select a whole old/new set, including ambiguous terminal fsync. */
 for(i=1;i<=24;i++){
  int result,recovered;
  setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;sync_count=0;sync_fail=i;
  result=install_transaction_run(&t,&p);sync_fail=0;
  recovered=install_transaction_recover(&t);
  CHECK(recovered==INSTALL_COMPLETED||recovered==INSTALL_ROLLED_BACK||recovered==INSTALL_UNCHANGED);
  CHECK(result!=INSTALL_COMPLETED||recovered==INSTALL_COMPLETED);
  expect(&f,&p,recovered!=INSTALL_COMPLETED,0);install_plan_free(&p);
 }
 /* A second process interruption inside restoration remains repeatable. */
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;f.crash="activated";crash_run(&f,&p);
 f.crash="restore";f.index=2;
 {int status;pid_t child=fork();CHECK(child>=0);if(!child){(void)install_transaction_recover(&t);_exit(1);}CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);}
 f.crash=0;CHECK(install_transaction_recover(&t)==INSTALL_ROLLED_BACK);expect(&f,&p,1,0);install_plan_free(&p);
 /* A committed installer transaction must not undo later user changes. */
 setup(&f,&p,argv[1]);t.root=f.root;t.journal=f.journal;
 CHECK(install_transaction_run(&t,&p)==INSTALL_COMPLETED);
 b=file("later user settings",0600);CHECK(!install_file_publish(f.root,"marker",&b));
 CHECK(install_transaction_recover(&t)==INSTALL_COMPLETED);
 {install_file_t current;CHECK(!install_file_read(f.root,"marker",&current));CHECK(install_file_equal(&b,&current));install_file_free(&current);}
 install_plan_free(&p);
 printf("installer transaction: %u checks passed; no network/service commands executed\n",checks);return 0;
}
