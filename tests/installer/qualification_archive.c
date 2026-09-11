#define INSTALL_ARCHIVE_MAIN unused_archive_main
#include "managed_archive_probe.c"
#include "qualification_cut.h"
int main(int argc,char **argv){
 install_package_t p;install_context_t c;fake_t f;pid_t child;int status;unsigned int i;
 CHECK(argc==4);memset(&observation,0,sizeof(observation));
 for(i=0;i<2;i++)observation.lan[i].present=observation.lan[i].up=1;
 strcpy(observation.lan[0].address,"10.0.2.15");strcpy(observation.lan[0].netmask,"255.255.240.0");strcpy(observation.lan[0].broadcast,"10.0.2.255");
 strcpy(observation.lan[1].address,"192.168.3.127");strcpy(observation.lan[1].netmask,"255.255.255.0");strcpy(observation.lan[1].broadcast,"192.168.3.255");
 observation.default_lan=observation.default_routes=1;strcpy(observation.gateway,"10.0.0.1");strcpy(observation.dns[0],"10.0.0.1");strcpy(observation.dns[1],"10.0.0.3");
 memset(&environment,0,sizeof(environment));environment.read_network=observe_archive;
 CHECK(!install_package_read(argv[2],&p,&stage));memset(&f,0,sizeof(f));f.cf=f.running=1;
 child=fork();CHECK(child>=0);if(!child){context_init(&c,&f,argv[1]);c.boundary=qualification_cut;c.boundary_context=&c;
  status=install_orchestrate(&c,&p);fprintf(stderr,"missed cut result=%d stage=%s\n",status,c.stage);_exit(78);}
 CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);
 context_init(&c,&f,argv[1]);f.cf=0;CHECK(install_recover_entry(&c,1,"start")==INSTALL_WAIT_CF);CHECK(f.entries==0);
 install_context_release(&c);context_init(&c,&f,argv[1]);
 f.cf=1;CHECK(install_recover_only(&c)==INSTALL_ROLLED_BACK);CHECK(f.running);
 CHECK(!install_transaction_load(&c.transaction,&c.plan));equal_set(&c,1);
 CHECK(install_recover_only(&c)==INSTALL_ROLLED_BACK);equal_set(&c,1);
 install_context_release(&c);install_package_free(&p);
 /* Put the original recovery ELF back through the real supported repair path;
  * this must not restart services or change a plan member. */
 CHECK(!install_package_read(argv[3],&p,&stage));
 {unsigned int stops=f.stops,starts=f.starts;install_file_t restored;
  context_init(&c,&f,argv[1]);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);
  CHECK(!strcmp(c.stage,"repair-recovery-executable"));CHECK(f.stops==stops&&f.starts==starts);equal_set(&c,0);
  CHECK(!install_file_read(c.state_directory,"recovery",&restored));CHECK(install_file_equal(&restored,&p.payload[0]));install_file_free(&restored);install_context_release(&c);
  context_init(&c,&f,argv[1]);CHECK(install_orchestrate(&c,&p)==INSTALL_UNCHANGED);CHECK(f.stops==stops&&f.starts==starts);install_context_release(&c);
 }
 install_package_free(&p);
 printf("trial8 archive deterministic cut/recovery: %u assertions; fake services and CF\n",checks);return 0;
}
