#define main payload_test_main
#include "test_gateway_dhcp_client.c"
#undef main
#include "network/gateway_network_owner.h"
typedef struct fake_owner {
    gateway_network_observation_t live,pending;
    unsigned char packet[2][600];size_t length[2];
    unsigned int observe_error;
    unsigned int opens[2],closes[2],writes,cancels,probes,replies,job,canceled,hang;
} fake_owner_t;
static int observe(void *c,gateway_network_observation_t *o){*o=((fake_owner_t *)c)->live;return ((fake_owner_t *)c)->observe_error?-1:0;}
static int open_client(void *c,unsigned int i){++((fake_owner_t *)c)->opens[i];return 0;}
static void close_client(void *c,unsigned int i){++((fake_owner_t *)c)->closes[i];}
static int receive_packet(void *c,unsigned int i,unsigned char *out,size_t cap)
{fake_owner_t *f=c;size_t n=f->length[i];CHECK(n<=cap);if(n)memcpy(out,f->packet[i],n);f->length[i]=0;return (int)n;}
static int send_packet(void *c,unsigned int i,gateway_dhcp_client_t *d,core_tick_t now)
{
    fake_owner_t *f=c;unsigned int type=d->send_type;unsigned char payload[512];size_t n;
    n=gateway_dhcp_broadcast(d,f->packet[i],sizeof(f->packet[i]),now);CHECK(n==342U);
    if(f->replies&&(type==1U||type==3U)){
        reply(payload,d->xid,type==1U?2U:5U,60);memcpy(payload+28,d->mac,6);
        memcpy(f->packet[i]+42,payload,300);memcpy(f->packet[i],d->mac,6);
        f->packet[i][11]=254;f->packet[i][35]=67;f->packet[i][37]=68;f->length[i]=n;
    }
    return 0;
}
static int probe(void *c,unsigned int i,const gateway_dhcp_client_t *d,unsigned int announce)
{fake_owner_t *f=c;(void)i;(void)d;(void)announce;++f->probes;return 0;}
static int apply(void *c,const gateway_network_observation_t *o,const char *resolver)
{fake_owner_t *f=c;(void)resolver;CHECK(!f->job);f->pending=*o;f->job=1;f->canceled=0;return 0;}
static int poll_job(void *c)
{fake_owner_t *f=c;CHECK(f->job);if(f->hang&&!f->canceled)return 0;f->job=0;if(f->canceled)return -1;f->live=f->pending;++f->writes;return 1;}
static int cancel(void *c){fake_owner_t *f=c;CHECK(f->job);++f->cancels;f->canceled=1;return 0;}
static const gateway_network_owner_ops_t ops={observe,open_client,close_client,receive_packet,send_packet,probe,apply,poll_job,cancel};
static void setup(gateway_network_owner_t *o,fake_owner_t *f,gateway_network_profile_t *p)
{
    static const char doc[]="auto eth0 eth1\niface eth0 inet static\n address 10.0.2.13\n netmask 255.255.240.0\n broadcast 10.0.2.255\n gateway 10.0.0.1\niface eth1 inet static\n address 192.0.2.127\n netmask 255.255.255.0\n broadcast 192.0.2.255\n";
    unsigned int i;memset(f,0,sizeof(*f));memset(p,0,sizeof(*p));strcpy(p->interfaces,doc);strcpy(p->resolver,"nameserver 10.0.0.1\n");
    CHECK(gateway_network_import(doc,strlen(doc),p->resolver,strlen(p->resolver),&p->settings)==0);
    for(i=0;i<8U;++i)strcpy(p->original_bind[i],"0.0.0.0");
    for(i=0;i<2U;++i){f->live.lan[i].present=f->live.lan[i].up=f->live.lan[i].link=1;memcpy(f->live.lan[i].mac,mac,6);
        f->live.lan[i].mac[5]=(unsigned char)(i+1U);strcpy(f->live.lan[i].address,p->settings.lan[i].address);
        strcpy(f->live.lan[i].netmask,p->settings.lan[i].netmask);CHECK(gateway_network_profile_broadcast(p,i,f->live.lan[i].broadcast)==0);}
    f->live.default_lan=f->live.default_routes=1;strcpy(f->live.gateway,"10.0.0.1");strcpy(f->live.dns[0],"10.0.0.1");
    CHECK(gateway_network_owner_init(o,&ops,f,100)==0);f->replies=1;
}
static void steps(gateway_network_owner_t *o,core_tick_t from,core_tick_t to)
{core_tick_t now;for(now=from;now<to;now+=10U)gateway_network_owner_step(o,now);}
#ifndef OWNER_TEST_ENTRY
#define OWNER_TEST_ENTRY main
#endif
int OWNER_TEST_ENTRY(void)
{
    gateway_network_owner_t o;fake_owner_t f;gateway_network_profile_t baseline,dhcp;gateway_network_settings_t settings;
    unsigned int writes,opens;core_tick_t expiry;
    setup(&o,&f,&baseline);settings=baseline.settings;settings.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;
    settings.default_lan=2;settings.automatic_dns=1;
    CHECK(gateway_network_profile_candidate(&baseline,&settings,&dhcp)==0);
    CHECK(gateway_network_owner_policy(&o,&dhcp,0)==0);steps(&o,0,10000);
    CHECK(o.ready&&o.dhcp[1].valid&&!strcmp(f.live.lan[1].address,"192.0.2.100"));
    CHECK(!strcmp(f.live.lan[0].address,"10.0.2.13")&&!strcmp(f.live.lan[0].broadcast,"10.0.2.255"));
    CHECK(f.live.default_lan==2&&!strcmp(f.live.dns[0],"192.0.2.1"));
    writes=f.writes;steps(&o,10000,11000);CHECK(f.writes==writes);
    /* A successful renewal of the same effective network must stay ready on
     * every owner step, not just after its transient write job has finished. */
    {core_tick_t now,renew=o.dhcp[1].renew_at;unsigned int before=f.writes;
        for(now=renew;now<renew+500U;now+=10U){gateway_network_owner_step(&o,now);CHECK(o.ready&&o.settled&&o.dhcp[1].valid);}
        CHECK(f.writes==before);CHECK(o.dhcp[1].expires_at==renew+60000U);
    }
    opens=f.opens[1];expiry=o.dhcp[1].expires_at;f.replies=0;
    f.observe_error=1;gateway_network_owner_step(&o,expiry);
    CHECK(!o.dhcp[1].valid&&!o.ready&&o.error);
    f.observe_error=0;steps(&o,expiry+10U,expiry+100U);
    CHECK(!o.dhcp[1].valid&&!f.live.lan[1].address[0]&&!f.live.default_lan&&!f.live.dns[0][0]);
    CHECK(f.opens[1]==opens);
    CHECK(gateway_network_owner_policy(&o,&baseline,expiry+100U)==0);steps(&o,expiry+100U,expiry+1000U);
    CHECK(o.ready&&!strcmp(f.live.lan[1].address,"192.0.2.127")&&f.closes[1]==1U);
    /* An old mutation cannot finish after a newer policy's restoration. */
    f.hang=1;CHECK(gateway_network_owner_policy(&o,&dhcp,70000)==0);gateway_network_owner_step(&o,70000);
    CHECK(f.job);writes=f.writes;CHECK(gateway_network_owner_policy(&o,&baseline,70001)==0);
    CHECK(f.cancels==1U);f.hang=0;steps(&o,70010,71000);
    CHECK(o.ready&&f.writes==writes+1U&&!strcmp(f.live.lan[1].address,"192.0.2.127"));
    CHECK(gateway_network_owner_policy(&o,&dhcp,72000)==0);gateway_network_owner_step(&o,72000);
    gateway_network_owner_stop(&o);steps(&o,72010,73000);opens=f.opens[1];steps(&o,73000,74000);
    CHECK(f.opens[1]==opens&&!o.dhcp[1].valid&&!f.live.lan[1].address[0]);
    /* Renewing and rebinding keep a still-valid address, but an actual NAK
     * withdraws it even when no policy was edited. */
    setup(&o,&f,&baseline);settings=baseline.settings;settings.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;
    CHECK(gateway_network_profile_candidate(&baseline,&settings,&dhcp)==0);
    CHECK(gateway_network_owner_policy(&o,&dhcp,0)==0);steps(&o,0,10000);
    f.replies=0;writes=f.writes;
    {core_tick_t now;for(now=o.dhcp[1].renew_at;now<o.dhcp[1].expires_at;now+=10U){
        gateway_network_owner_step(&o,now);CHECK(o.ready&&o.dhcp[1].valid);}
        CHECK(o.dhcp[1].state==GATEWAY_DHCP_REBINDING&&f.writes==writes);
        {unsigned char p[512];size_t n=reply(p,o.dhcp[1].xid,6,60);memcpy(p+28,o.dhcp[1].mac,6);
            CHECK(gateway_dhcp_receive(&o.dhcp[1],p,n,now-1U)==0);}
        steps(&o,now,now+500U);CHECK(!o.ready&&!o.dhcp[1].valid&&!f.live.lan[1].address[0]);
    }
    printf("network DHCP owner: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
