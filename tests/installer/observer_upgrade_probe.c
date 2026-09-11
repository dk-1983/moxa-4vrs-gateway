/* Actual installer orchestration; process/device boundaries are fake. */
#define INSTALL_ARCHIVE_MAIN baseline_archive_main
#include "managed_archive_probe.c"
int main(int argc,char **argv){
 install_package_t p;install_context_t c;fake_t f;install_file_t config,after;
 unsigned int i,stops,starts;int r;char *baseline[4];
 CHECK(argc==5);
 baseline[0]=argv[0];baseline[1]=argv[1];baseline[2]=argv[2];baseline[3]="complete";
 CHECK(!baseline_archive_main(4,baseline));
 CHECK(!install_file_read(argv[1],"var/hda/4vrs/config/gateway.conf",&config));
 CHECK(!install_package_read(argv[3],&p,&stage));
 memset(&f,0,sizeof(f));f.cf=f.running=1;
 f.fail_start=!strcmp(argv[4],"rollback");context_init(&c,&f,argv[1]);
 r=install_orchestrate(&c,&p);
 printf("064851 upgrade result=%d stage=%s stops=%u starts=%u\n",r,c.stage,f.stops,f.starts);
 CHECK(r==(f.fail_start?INSTALL_ROLLED_BACK:INSTALL_COMPLETED));
 CHECK(f.stops>0);equal_set(&c,f.fail_start);
 /* No RNG bootstrap/state, trial policy, admin or TLS material is an
  * installer transaction member. No secret/state fixture is generated. */
 for(i=0;i<c.plan.count;i++){
  const char *path=c.plan.member[i].path;
  CHECK(!strstr(path,"security")&&!strstr(path,"4vrs-rng/")&&!strstr(path,"trial")&&!strstr(path,"seed"));
 }
 CHECK(!install_file_read(argv[1],"var/hda/4vrs/config/gateway.conf",&after));
 CHECK(install_file_equal(&config,&after));install_file_free(&after);
 if(f.fail_start){
  install_context_release(&c);context_init(&c,&f,argv[1]);
  CHECK(install_recover_only(&c)==INSTALL_ROLLED_BACK);
  f.fail_start=0;install_context_release(&c);context_init(&c,&f,argv[1]);
  CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);equal_set(&c,0);
 }
 stops=f.stops;starts=f.starts;install_context_release(&c);context_init(&c,&f,argv[1]);
 CHECK(install_orchestrate(&c,&p)==INSTALL_UNCHANGED);
 CHECK(stops==f.stops&&starts==f.starts);
 CHECK(!install_file_read(argv[1],"var/hda/4vrs/config/gateway.conf",&after));
 CHECK(install_file_equal(&config,&after));install_file_free(&after);
 install_context_release(&c);install_package_free(&p);
 /* A successful update can also be reversed by the complete saved package. */
 CHECK(!install_package_read(argv[2],&p,&stage));context_init(&c,&f,argv[1]);
 CHECK(install_orchestrate(&c,&p)==INSTALL_COMPLETED);equal_set(&c,0);
 CHECK(!install_file_read(argv[1],"var/hda/4vrs/config/gateway.conf",&after));
 CHECK(install_file_equal(&config,&after));install_file_free(&after);
 stops=f.stops;starts=f.starts;install_context_release(&c);context_init(&c,&f,argv[1]);
 CHECK(install_orchestrate(&c,&p)==INSTALL_UNCHANGED);
 CHECK(stops==f.stops&&starts==f.starts);
 install_context_release(&c);install_package_free(&p);install_file_free(&config);
 printf("064851 upgrade/rollback/retry/no-op: %u checks PASS; hardware fake, no RNG process\n",checks);
 return 0;
}
