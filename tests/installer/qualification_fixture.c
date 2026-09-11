#define _GNU_SOURCE
#define INSTALL_ORCHESTRATOR_MAIN unused_orchestration_main
#include "test_orchestrator.c"
#include "qualification_cut.h"
#include <limits.h>

/* Deliberately no production platform entry in this main. The fixture uses
 * real files/journal/bootstrap/recovery, fake services and minimal profiles. */
int main(int argc,char **argv){
 char base[1024],root[1024],real[PATH_MAX];install_package_t p;install_context_t c;
 fake_t f;install_file_t event;pid_t child;int status;unsigned int i;
#ifdef QUALIFICATION_HOST
 const char *parent="/workspace/build";
#else
 const char *parent="/var/hda/4vrs/tests";
#endif
 if(argc!=2||strcmp(argv[1],"--isolated-first-and-cut"))return 2;
 /* Resolve only this fixed parent; refuse aliases, existing output, and all
 * arbitrary roots. Never unlink or touch the live installer/network store. */
 if(!realpath(parent,real)||strcmp(real,parent))return 2;
 if(snprintf(base,sizeof(base),"%s/installer-qualification-01",parent)>=(int)sizeof(base)||mkdir(base,0700))return 2;
 alarm(120);
 memset(&environment,0,sizeof(environment));environment.read_network=observe;environment.read_lan2=lan;
 fixture(base,root,&p);memset(&f,0,sizeof(f));f.cf=1;
 context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);
 equal_set(&c,0);CHECK(f.stops==1&&f.starts==1);install_context_release(&c);
 context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_UNCHANGED);
 CHECK(f.stops==1&&f.starts==1);install_context_release(&c);
 /* Changed script forces a genuine transaction, rather than recovery-only
 * bootstrap repair on an otherwise identical healthy installation. */
 p.payload[2].data=(unsigned char*)"#!/bin/sh\n# qualification-only\nexit 0\n";
 p.payload[2].size=strlen((char*)p.payload[2].data);
 child=fork();CHECK(child>=0);
 if(!child){context_init(&c,&f,root);c.boundary=qualification_cut;c.boundary_context=&c;
  (void)install_orchestrate(&c,&p);_exit(78);}
 CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);
 context_init(&c,&f,root);CHECK(!install_layout(&c,0));
 CHECK(!install_file_read(c.state_directory,"qualification-event",&event));
 CHECK(event.kind==1&&event.size<384&&strstr((char*)event.data,"boundary=publish-application\n"));install_file_free(&event);
 CHECK(install_transaction_status(&c.transaction)!=INSTALL_COMPLETED);
 f.cf=0;CHECK(install_recover_entry(&c,1,"start")==INSTALL_WAIT_CF);CHECK(f.entries==0);
 install_context_release(&c);context_init(&c,&f,root);
 f.cf=1;CHECK(install_recover_only(&c)==INSTALL_ROLLED_BACK);CHECK(f.running);
 CHECK(!install_transaction_load(&c.transaction,&c.plan));equal_set(&c,1);
 for(i=0;i<c.plan.count;i++)if(!strcmp(c.plan.member[i].path,"etc/4vrs-installer/application"))
  CHECK(!install_file_equal(&c.plan.member[i].before,&c.plan.member[i].after));
 install_context_release(&c);
 /* Repeat recovery is terminal and must not activate the rejected package. */
 context_init(&c,&f,root);CHECK(install_recover_only(&c)==INSTALL_ROLLED_BACK);install_context_release(&c);
 /* An unrecordable boundary must roll back normally, not fake exit 77. */
 fixture(base,root,&p);memset(&f,0,sizeof(f));f.cf=1;
 context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);install_context_release(&c);
 directory(root,"etc/4vrs-installer/qualification-event");
 p.payload[2].data=(unsigned char*)"#!/bin/sh\n# failed event\nexit 0\n";p.payload[2].size=strlen((char*)p.payload[2].data);
 context_init(&c,&f,root);c.boundary=qualification_cut;c.boundary_context=&c;
 CHECK(install_orchestrate(&c,&p)==INSTALL_ROLLED_BACK);equal_set(&c,1);install_context_release(&c);
 run_crash(base,"bootstrap-executable",0,0);run_crash(base,"active",0,1);
 alarm(0);printf("PASS qualification fixture: %u assertions; real journal/files; fake services/CF; no live installation\n",checks);
 return 0;
}
