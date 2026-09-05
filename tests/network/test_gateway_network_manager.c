#include <stdio.h>
#include <string.h>
#include "network/gateway_network_manager.h"

static unsigned int checks,failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while (0)
typedef struct fake {int stage,apply,poll,commit,restore,discard;unsigned int stages,applies,commits,restores,discards;} fake_t;
static int stage(void *x,const gateway_network_settings_t *s){fake_t*f=x;(void)s;++f->stages;return f->stage;}
static int apply(void *x){fake_t*f=x;++f->applies;return f->apply;}
static int poll_net(void *x){return ((fake_t*)x)->poll;}
static int commit(void *x){fake_t*f=x;++f->commits;return f->commit;}
static int restore(void *x){fake_t*f=x;++f->restores;return f->restore;}
static int discard(void *x){fake_t*f=x;++f->discards;return f->discard;}
static const gateway_network_manager_ops_t ops={stage,apply,poll_net,commit,restore,discard};
static void setup(gateway_network_manager_t *m,fake_t *f,gateway_network_settings_t *s)
{
    memset(f,0,sizeof(*f));f->poll=1;
    gateway_network_settings_init(s);
    strcpy(s->lan[0].address,"10.0.2.13");strcpy(s->lan[0].netmask,"255.255.240.0");
    strcpy(s->lan[1].address,"192.168.4.127");strcpy(s->lan[1].netmask,"255.255.255.0");
    CHECK(gateway_network_manager_init(m,&ops,f)==0);
}
static void ready(gateway_network_manager_t *m,core_tick_t t)
{gateway_network_manager_step(m,t);CHECK(m->state==GATEWAY_NETWORK_WAIT_BINDINGS);m->bindings_ready=1;gateway_network_manager_step(m,t);CHECK(m->state==GATEWAY_NETWORK_WAIT_CONFIRM);}
int main(void)
{
    gateway_network_manager_t m;fake_t f;gateway_network_settings_t s;unsigned int i;
    setup(&m,&f,&s);CHECK(gateway_network_manager_confirm(&m)!=0);
    CHECK(gateway_network_manager_apply(&m,&s,0)==0);CHECK(f.stages==1&&f.applies==1);
    CHECK(gateway_network_manager_apply(&m,&s,0)!=0);
    CHECK(gateway_network_manager_confirm(&m)!=0);ready(&m,10);
    CHECK(gateway_network_manager_confirm(&m)==0);gateway_network_manager_step(&m,20);
    CHECK(m.state==GATEWAY_NETWORK_COMMITTING&&f.commits==0);gateway_network_manager_step(&m,21);
    CHECK(m.state==GATEWAY_NETWORK_KEPT&&f.commits==1&&f.restores==0);
    setup(&m,&f,&s);f.stage=-1;CHECK(gateway_network_manager_apply(&m,&s,0)!=0);CHECK(!f.applies&&!f.restores);
    setup(&m,&f,&s);f.apply=-1;CHECK(gateway_network_manager_apply(&m,&s,0)!=0);CHECK(f.restores==1);
    gateway_network_manager_step(&m,1);CHECK(m.state==GATEWAY_NETWORK_ROLLBACK_BINDINGS);
    m.bindings_ready=1;gateway_network_manager_step(&m,2);CHECK(m.state==GATEWAY_NETWORK_REVERTED);
    for(i=0;i<3U;++i){
        setup(&m,&f,&s);CHECK(gateway_network_manager_apply(&m,&s,0xfffffff0U)==0);ready(&m,0xfffffff1U);
        if(i==0)gateway_network_manager_revert(&m);
        if(i==1)m.client_alive=0;
        gateway_network_manager_step(&m,i==2?0x0000ea51U:0xfffffff2U);
        CHECK(m.state==GATEWAY_NETWORK_ROLLING_BACK&&f.restores==1&&!f.commits);
        gateway_network_manager_step(&m,i==2?0x0000ea52U:0xfffffff3U);
        if(i==1)CHECK(m.state==GATEWAY_NETWORK_REVERTED); /* No dead client ACK required. */
    }
    setup(&m,&f,&s);CHECK(gateway_network_manager_apply(&m,&s,0)==0);f.poll=0;
    gateway_network_manager_step(&m,29999);CHECK(m.state==GATEWAY_NETWORK_APPLYING);
    gateway_network_manager_step(&m,30000);CHECK(m.state==GATEWAY_NETWORK_ROLLING_BACK);
    gateway_network_manager_step(&m,60000);CHECK(m.state==GATEWAY_NETWORK_ROLLBACK_FAILED);
    CHECK(gateway_network_manager_apply(&m,&s,60001)!=0);
    setup(&m,&f,&s);CHECK(gateway_network_manager_apply(&m,&s,0)==0);ready(&m,0);f.commit=-2;
    CHECK(gateway_network_manager_confirm(&m)==0);gateway_network_manager_step(&m,1);gateway_network_manager_step(&m,2);
    CHECK(m.state==GATEWAY_NETWORK_DURABILITY_UNCERTAIN&&!f.restores);
    CHECK(gateway_network_manager_apply(&m,&s,3)!=0);
    setup(&m,&f,&s);CHECK(gateway_network_manager_apply(&m,&s,0)==0);ready(&m,0);f.commit=-1;
    CHECK(gateway_network_manager_confirm(&m)==0);gateway_network_manager_step(&m,1);gateway_network_manager_step(&m,2);
    CHECK(m.state==GATEWAY_NETWORK_ROLLING_BACK&&f.restores==1);
    setup(&m,&f,&s);CHECK(gateway_network_manager_apply(&m,&s,0)==0);ready(&m,0);
    CHECK(gateway_network_manager_confirm(&m)==0);gateway_network_manager_step(&m,60000);
    CHECK(m.state==GATEWAY_NETWORK_ROLLING_BACK&&!f.commits); /* Deadline wins over late F3. */
    setup(&m,&f,&s);CHECK(gateway_network_manager_apply(&m,&s,0)==0);ready(&m,0);f.poll=-1;
    gateway_network_manager_step(&m,1);CHECK(m.state==GATEWAY_NETWORK_ROLLING_BACK);
    setup(&m,&f,&s);CHECK(gateway_network_manager_apply(&m,&s,0)==0);ready(&m,0);f.poll=0;
    CHECK(gateway_network_manager_confirm(&m)==0);gateway_network_manager_step(&m,1);
    CHECK(m.state==GATEWAY_NETWORK_ROLLING_BACK&&!f.commits); /* Acquisition/loss is not success. */
    printf("network manager checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
