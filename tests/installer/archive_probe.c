/* Qualification-only executable: never included in the target package. */
#define INSTALL_ORCHESTRATOR_MAIN orchestrator_fixture_main
#include "test_orchestrator.c"
static gateway_network_observation_t archived_observation;
static int archive_observe(void*v,gateway_network_observation_t*out){(void)v;*out=archived_observation;return 0;}
static int archive_lan(void*v,gateway_lan_observation_t*out){(void)v;*out=archived_observation.lan[1];return 0;}
int main(int argc,char**argv){
 static const char*inputs[]={"etc__network__interfaces","etc__resolv.conf","etc__init.d__networking","etc__init.d__ntpdate","etc__init.d__halt","etc__init.d__4vrs-gateway","var__hda__4vrs__config__gateway.conf","var__hda__4vrs__bin__4vrs-gateway"};
 static const char*dest[]={"etc/network/interfaces","etc/resolv.conf","etc/rc.d/init.d/networking","etc/rc.d/init.d/ntpdate","etc/rc.d/init.d/halt","etc/rc.d/init.d/4vrs-gateway","var/hda/4vrs/config/gateway.conf","var/hda/4vrs/bin/4vrs-gateway"};
 install_package_t p,dummy;install_context_t c;fake_t f;char root[1024];unsigned int i;FILE*observation;int r;
 CHECK(argc==5);observation=fopen(argv[4],"r");CHECK(observation!=0);memset(&archived_observation,0,sizeof(archived_observation));
 for(i=0;i<2;i++){gateway_lan_observation_t*l=&archived_observation.lan[i];l->present=l->up=1;CHECK(fscanf(observation,"%15s %15s %15s",l->address,l->netmask,l->broadcast)==3);}
 archived_observation.default_lan=archived_observation.default_routes=1;
 CHECK(fscanf(observation,"%15s %15s %15s",archived_observation.gateway,archived_observation.dns[0],archived_observation.dns[1])==3);CHECK(!fclose(observation));
 fixture(argv[2],root,&dummy);directory(root,"var/hda/4vrs/bin");
 for(i=0;i<8;i++){install_file_t file;CHECK(!install_file_read(argv[1],inputs[i],&file));CHECK(file.kind==1);file.mode=(i>=2&&i!=6)?0755:0600;CHECK(!install_file_publish(root,dest[i],&file));install_file_free(&file);}
 CHECK(!install_package_read(argv[3],&p,&stage));
 memset(&environment,0,sizeof(environment));environment.read_network=archive_observe;environment.read_lan2=archive_lan;
 memset(&f,0,sizeof(f));f.cf=1;f.running=1;context_init(&c,&f,root);
 r=install_orchestrate(&c,&p);stage=c.stage;CHECK(r==INSTALL_COMPLETED);equal_set(&c,0);
 CHECK(install_file_equal(&c.plan.member[1].before,&c.plan.member[1].after));CHECK(install_file_equal(&c.plan.member[2].before,&c.plan.member[2].after));
 install_context_release(&c);context_init(&c,&f,root);CHECK(install_orchestrate(&c,&p)==INSTALL_UNCHANGED);CHECK(!install_recover_entry(&c,0,"start"));CHECK(!install_recover_entry(&c,1,"start"));
 install_context_release(&c);install_package_free(&p);
 printf("immutable archive + actual package production orchestration: %u checks passed; hardware providers fake\n",checks);return 0;
}
