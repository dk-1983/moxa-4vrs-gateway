#define _GNU_SOURCE
#define OWNER_TEST_ENTRY owner_test_main
#include "test_gateway_network_owner.c"
#undef OWNER_TEST_ENTRY
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "network/gateway_network_service.h"
#include "network/gateway_network_store.h"
typedef struct shared_service {fake_owner_t fake;core_tick_t now;int guardian;} shared_service_t;
static int write_live(void *context,const gateway_network_observation_t *o,const char *resolver)
{shared_service_t *s=context;(void)resolver;s->fake.live=*o;++s->fake.writes;return 0;}
static core_tick_t service_clock(void *context){return ((shared_service_t *)context)->now;}
static void save(const char *dir,const gateway_network_profile_t *p,const char *name)
{char data[32768];size_t n;CHECK(gateway_network_profile_encode(p,data,sizeof(data),&n)==0);
 CHECK(gateway_network_store_write(dir,name,data,n)==GATEWAY_NET_STORE_OK);}
static int ready(int fd,shared_service_t *s,unsigned int request,gateway_network_service_status_t *status)
{
    unsigned int i;int result;
    for(i=0;i<4000U;++i){
        s->now+=10U;usleep(1000);result=gateway_network_service_read(fd,status);
        if(result<0)return -1;
        if(result==1&&status->request==request&&status->ready&&!status->error)return 0;
    }
    return -1;
}
int main(void)
{
    shared_service_t *s;gateway_network_owner_t owner;gateway_network_profile_t baseline,p;
    gateway_network_service_environment_t environment;gateway_network_service_status_t status;
    char directory[]="/tmp/4vrs-service-XXXXXX",name[256];unsigned int i,writes;int fd,other,exit_status;
    static const char *files[]={"owner.sock","owner.lock","confirmed","good","candidate","lock","write.tmp"};
    CHECK(mkdtemp(directory)!=0);
    s=mmap(0,sizeof(*s),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);CHECK(s!=MAP_FAILED);
    memset(s,0,sizeof(*s));setup(&owner,&s->fake,&baseline);save(directory,&baseline,"confirmed");save(directory,&baseline,"good");
    memset(&environment,0,sizeof(environment));environment.directory=directory;environment.observe=observe;environment.write=write_live;
    environment.client_ops=&ops;environment.context=s;environment.clock=service_clock;environment.started_guardian=&s->guardian;
    p=baseline;strcpy(p.settings.lan[1].address,"192.0.2.126");CHECK(gateway_network_profile_candidate(&baseline,&p.settings,&p)==0);save(directory,&p,"candidate");
    fd=gateway_network_service_connect(&environment,1);CHECK(fd>=0&&s->guardian>0);
    CHECK(gateway_network_service_command(fd,'A',1)==0);CHECK(ready(fd,s,1,&status)==0);
    CHECK(!strcmp(s->fake.live.lan[1].address,"192.0.2.126"));
    /* Another client cannot acquire mutation ownership while this peer lives. */
    other=gateway_network_service_connect(&environment,0);CHECK(other>=0);usleep(30000);
    CHECK(gateway_network_service_read(other,&status)<0);close(other);
    CHECK(gateway_network_service_command(fd,'R',2)==0);CHECK(ready(fd,s,2,&status)==0);
    CHECK(!strcmp(s->fake.live.lan[1].address,"192.0.2.127"));
    CHECK(gateway_network_service_command(fd,'A',3)==0);CHECK(ready(fd,s,3,&status)==0);
    close(fd);
    for(i=0;i<2000U&&strcmp(s->fake.live.lan[1].address,"192.0.2.127");++i){s->now+=10U;usleep(1000);}
    CHECK(!strcmp(s->fake.live.lan[1].address,"192.0.2.127"));
    usleep(30000);fd=gateway_network_service_connect(&environment,0);CHECK(fd>=0);
    CHECK(gateway_network_service_command(fd,'A',4)==0);CHECK(ready(fd,s,4,&status)==0);
    CHECK(kill(status.owner_pid,SIGKILL)==0);close(fd);
    for(i=0;i<3000U&&strcmp(s->fake.live.lan[1].address,"192.0.2.127");++i){s->now+=10U;usleep(1000);}
    CHECK(!strcmp(s->fake.live.lan[1].address,"192.0.2.127"));
    usleep(100000);p=baseline;p.settings.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;p.settings.default_lan=2;p.settings.automatic_dns=1;
    CHECK(gateway_network_profile_candidate(&baseline,&p.settings,&p)==0);save(directory,&p,"candidate");
    fd=gateway_network_service_connect(&environment,0);CHECK(fd>=0);CHECK(gateway_network_service_command(fd,'A',5)==0);
    CHECK(ready(fd,s,5,&status)==0);CHECK(status.lease_valid[1]&&!strcmp(status.observed.lan[1].address,"192.0.2.100"));
    CHECK(gateway_network_service_command(fd,'R',6)==0);CHECK(ready(fd,s,6,&status)==0);
    close(fd);CHECK(kill(s->guardian,SIGTERM)==0);CHECK(waitpid(s->guardian,&exit_status,0)==s->guardian);
    CHECK(WIFEXITED(exit_status)&&!WEXITSTATUS(exit_status));writes=s->fake.writes;usleep(100000);CHECK(writes==s->fake.writes);
    CHECK(!strcmp(s->fake.live.lan[1].address,"192.0.2.127"));
    for(i=0;i<sizeof(files)/sizeof(files[0]);++i){snprintf(name,sizeof(name),"%s/%s",directory,files[i]);unlink(name);}
    CHECK(rmdir(directory)==0);munmap(s,sizeof(*s));
    printf("network service processes: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
