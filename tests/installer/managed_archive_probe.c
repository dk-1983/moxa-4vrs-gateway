/* Qualification entry only: actual orchestration, fake device boundaries. */
#define INSTALL_ORCHESTRATOR_MAIN orchestrator_fixture_main
#include "test_orchestrator.c"
static gateway_network_observation_t observation;
static int observe_archive(void*v,gateway_network_observation_t*out){(void)v;*out=observation;return 0;}
int main(int argc,char**argv){
 install_package_t p;install_context_t c;fake_t f;unsigned int i,stops,starts;int r;
 CHECK(argc==4);memset(&observation,0,sizeof(observation));
 for(i=0;i<2;i++){observation.lan[i].present=observation.lan[i].up=1;}
 strcpy(observation.lan[0].address,"10.0.2.15");strcpy(observation.lan[0].netmask,"255.255.240.0");strcpy(observation.lan[0].broadcast,"10.0.2.255");
 strcpy(observation.lan[1].address,"192.168.3.127");strcpy(observation.lan[1].netmask,"255.255.255.0");strcpy(observation.lan[1].broadcast,"192.168.3.255");
 observation.default_lan=observation.default_routes=1;strcpy(observation.gateway,"10.0.0.1");strcpy(observation.dns[0],"10.0.0.1");strcpy(observation.dns[1],"10.0.0.3");
 memset(&environment,0,sizeof(environment));environment.read_network=observe_archive;
 CHECK(!install_package_read(argv[2],&p,&stage));memset(&f,0,sizeof(f));f.cf=f.running=1;context_init(&c,&f,argv[1]);
 if(strcmp(argv[3],"refuse")){
  install_file_t saved,absent,active_before;CHECK(!install_file_read(argv[1],"etc/init.d/ntpdate",&saved));
  CHECK(!install_layout(&c,1));CHECK(!install_file_read(argv[1],"etc/4vrs-installer/active",&active_before));
  script(argv[1],"etc/init.d/ntpdate","#!/bin/sh\n# /etc/4vrs-clock-managed\nexit 0\n");
  CHECK(install_orchestrate(&c,&p)==INSTALL_REFUSED);CHECK(!strcmp(c.stage,"plan-clock-ntpdate")&&c.detail==INSTALL_CLOCK_GUARD);CHECK(f.stops==0);
  CHECK(!install_file_read(argv[1],"etc/4vrs-installer/active",&absent));CHECK(install_file_equal(&active_before,&absent));install_file_free(&absent);install_file_free(&active_before);
  CHECK(!install_file_publish(argv[1],"etc/init.d/ntpdate",&saved));install_file_free(&saved);
  /* Actual native plan left slot0/work but no active, same boundary as trial5.
   * The archive did not include installer preparation files; do not claim it did. */
  install_context_release(&c);context_init(&c,&f,argv[1]);
 }
 if(!strcmp(argv[3],"rollback"))f.fail_start=1;
 r=install_orchestrate(&c,&p);printf("result=%d stage=%s detail=%d stops=%u starts=%u\n",r,c.stage,c.detail,f.stops,f.starts);fflush(stdout);
 if(!strcmp(argv[3],"refuse")){CHECK(r==-1);CHECK(!strcmp(c.stage,"plan-clock"));CHECK(f.stops==0);}
 else {
  CHECK(r==(!strcmp(argv[3],"rollback")?INSTALL_ROLLED_BACK:INSTALL_COMPLETED));equal_set(&c,r==INSTALL_ROLLED_BACK);
  for(i=0;i<c.plan.count;i++){const char*s=c.plan.member[i].path;if(strstr(s,"ntpdate")||strstr(s,"halt")||!strcmp(s,"etc/4vrs-clock-managed")||i<5)CHECK(install_file_equal(&c.plan.member[i].before,&c.plan.member[i].after));}
  if(r==INSTALL_ROLLED_BACK){install_context_release(&c);context_init(&c,&f,argv[1]);CHECK(install_recover_only(&c)==INSTALL_ROLLED_BACK);f.fail_start=0;install_context_release(&c);context_init(&c,&f,argv[1]);CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);}
  stops=f.stops;starts=f.starts;install_context_release(&c);context_init(&c,&f,argv[1]);CHECK(install_orchestrate(&c,&p)==INSTALL_UNCHANGED);CHECK(f.stops==stops&&f.starts==starts);
 }
 install_context_release(&c);install_package_free(&p);printf("fresh managed archive: %u checks passed; hardware fake\n",checks);return 0;
}
