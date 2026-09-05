#define _GNU_SOURCE
#define main original_application_test_main
#include "test_gateway_application.c"
#undef main
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include "network/gateway_network_store.h"
#include "network/gateway_network_boot.h"
#include "panel/gateway_panel.h"
#include "network/gateway_dhcp_wire.h"
#include "../network/dhcp_bench_packets.h"

typedef struct fake_kernel {
    gateway_lan_observation_t lan;
    gateway_network_observation_t all;
    core_tick_t now;
    unsigned int writes,fail_apply,fail_restore,hang_apply,leaked_fd,fail_store_sync;
    unsigned int hang_sync_after,sync_calls;int sync_pid;
    int sentinel;
    int apply_pid,restore_pid,supervisor_pid;
    core_tick_t restore_started;
    int guardian_pid;
    unsigned int dhcp_replies,dhcp_opens[2],dhcp_closes[2];
    unsigned int bench,renew_sent,renew_received;core_tick_t bench_renew,bench_expiry;
    unsigned char packet[2][600];size_t packet_length[2];
} fake_kernel_t;
static fake_kernel_t *kernel;
static unsigned int use_canonical;
static unsigned int use_full;
static unsigned int use_service,use_boot_path;
static unsigned int boot_lo,boot_eth2_calls,boot_present_eth2;
static int boot_lo_state,boot_receipt_valid;
static char boot_receipt_text[GATEWAY_NETWORK_FILE_MAX+1];
static int boot_observe(void *c){(void)c;return boot_lo_state;}
static int boot_receipt(void *c,unsigned int op,const char *text,size_t n)
{(void)c;if(!op)return boot_receipt_valid&&strlen(boot_receipt_text)==n&&!memcmp(text,boot_receipt_text,n);boot_receipt_valid=op==2;if(op==2){memcpy(boot_receipt_text,text,n);boot_receipt_text[n]=0;}return 0;}
static int boot_lo_action(void *c,unsigned int up){(void)c;++boot_lo;boot_lo_state=up?1:3;return 0;}
static int boot_loopback(void *c,const char *text,size_t n,unsigned int up)
{const gateway_network_loopback_ops_t ops={boot_observe,boot_receipt,boot_lo_action,0};return gateway_network_loopback(text,n,up,&ops,c);}
static int boot_present(void *c,const char *n){(void)c;return strcmp(n,"eth2")?1:(int)boot_present_eth2;}
static int boot_action(void *c,const char *n,unsigned int up){(void)c;CHECK(up==1);if(!strcmp(n,"lo"))++boot_lo;else if(!strcmp(n,"eth2"))++boot_eth2_calls;else CHECK(0);return 0;}
static const gateway_network_boot_ops_t boot_ops={boot_present,boot_action,0,boot_loopback};
static gateway_network_service_environment_t service_environment;
int __real_fsync(int);
int __wrap_fsync(int fd)
{if(kernel&&kernel->hang_sync_after&&++kernel->sync_calls==kernel->hang_sync_after){kernel->sync_pid=(int)getpid();for(;;)pause();}
if(kernel&&kernel->fail_store_sync){kernel->fail_store_sync=0;errno=EIO;return -1;}return __real_fsync(fd);}
ssize_t __real_recv(int,void *,size_t,int);
ssize_t __wrap_recv(int fd,void *bytes,size_t size,int flags)
{
    ssize_t n=__real_recv(fd,bytes,size,flags);
    if(n==0&&getenv("LAN2_LEGACY_PEER")){
        int type=0;socklen_t length=sizeof(type);
        if(!getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&length)&&type==SOCK_SEQPACKET){errno=EAGAIN;return -1;}
    }
    return n;
}
static gateway_network_environment_t environment;
static char directory[64],interfaces_path[256],resolver_path[256],trace_path[256];
static core_tick_t wall_ms(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (core_tick_t)t.tv_sec*1000U+(core_tick_t)(t.tv_nsec/1000000L);}
static core_tick_t net_clock(void *context){return ((fake_kernel_t *)context)->now;}
static int net_read(void *context,gateway_lan_observation_t *out){*out=((fake_kernel_t *)context)->lan;return 0;}
static int net_write(void *context,const gateway_lan_observation_t *in)
{
    fake_kernel_t *k=context;++k->writes;
    if(!strcmp(in->address,"192.168.4.126"))k->apply_pid=(int)getpid();
    else {k->restore_pid=(int)getpid();k->restore_started=wall_ms();}
    if(k->sentinel>2&&fcntl(k->sentinel,F_GETFD)>=0)k->leaked_fd=1;
    if(k->hang_apply&&!strcmp(in->address,"192.168.4.126"))for(;;)pause();
    /* Simulate a partial mutation, then report a provider error. */
    k->lan=*in;
    return (!strcmp(in->address,"192.168.4.126")?k->fail_apply:k->fail_restore)?-1:0;
}
static int full_read(void *context,gateway_network_observation_t *out)
{*out=((fake_kernel_t *)context)->all;return 0;}
static int full_write(void *context,const gateway_network_observation_t *in,const char *resolver)
{
    fake_kernel_t *k=context;FILE *f;
    int result=net_write(context,&in->lan[1]);
    k->all=*in;
    f=fopen(environment.resolver_path,"w");
    if(!f)return -1;
    if(fputs(resolver,f)<0){fclose(f);return -1;}
    if(fclose(f))return -1;
    return result;
}
static int dhcp_open(void *context,unsigned int lan){++((fake_kernel_t *)context)->dhcp_opens[lan];return 0;}
static void dhcp_close(void *context,unsigned int lan){++((fake_kernel_t *)context)->dhcp_closes[lan];}
static int dhcp_receive(void *context,unsigned int lan,unsigned char *out,size_t cap)
{fake_kernel_t *k=context;size_t n=k->packet_length[lan];if(n>cap)return -1;if(n){memcpy(out,k->packet[lan],n);if(k->bench&&out[42+12])++k->renew_received;}k->packet_length[lan]=0;return (int)n;}
static void put_number(unsigned char *p,uint32_t n)
{p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;}
static int dhcp_send(void *context,unsigned int lan,gateway_dhcp_client_t *d,core_tick_t now)
{
    fake_kernel_t *k=context;unsigned int type=d->send_type;unsigned char *frame=k->packet[lan],*p=frame+42;size_t n,at=240;
    uint32_t address=lan?0xc0a80464UL:0x0a000264UL,router=lan?0xc0a80401UL:0x0a000001UL;
    if(k->bench&&d->valid){++k->renew_sent;k->bench_renew=d->renew_at;k->bench_expiry=d->expires_at;}
    n=gateway_dhcp_broadcast(d,frame,600,now);if(!n)return 0;
    if(k->bench){if(k->dhcp_replies&&(type==1U||type==3U))k->packet_length[lan]=bench_reply(d,type,frame);return 0;}
    if(!k->dhcp_replies||(type!=1U&&type!=3U))return 0;
    memset(p,0,300);p[0]=2;p[1]=1;p[2]=6;put_number(p+4,d->xid);put_number(p+16,address);
    memcpy(p+28,d->mac,6);put_number(p+236,0x63825363UL);
    p[at++]=53;p[at++]=1;p[at++]=(unsigned char)(type==1U?2U:5U);
#define OPTION(code,value) do{p[at++]=code;p[at++]=4;put_number(p+at,value);at+=4;}while(0)
    OPTION(54,router);OPTION(1,lan?0xffffff00UL:0xfffff000UL);OPTION(3,router);OPTION(6,router);OPTION(51,60);
#undef OPTION
    p[at]=255;memcpy(frame,d->mac,6);frame[11]=254;frame[35]=67;frame[37]=68;k->packet_length[lan]=n;return 0;
}
static int dhcp_probe(void *context,unsigned int lan,const gateway_dhcp_client_t *d,unsigned int announce)
{(void)context;(void)lan;(void)d;(void)announce;return 0;}
static const gateway_network_owner_ops_t dhcp_ops={0,dhcp_open,dhcp_close,dhcp_receive,dhcp_send,dhcp_probe,0,0,0};
static void text_file(const char *path,const char *text)
{FILE *f=fopen(path,"w");CHECK(f!=0);if(f){CHECK(fputs(text,f)>=0);CHECK(fclose(f)==0);}}
static void copy_fixture(const char *name,const char *target)
{
    char path[1024],bytes[16384];FILE *file;size_t n;
    snprintf(path,sizeof(path),"%s/%s",getenv("LAN2_BASELINE"),name);
    file=fopen(path,"rb");CHECK(file!=0);if(!file)exit(1);
    n=fread(bytes,1,sizeof(bytes)-1U,file);CHECK(!ferror(file)&&feof(file));fclose(file);bytes[n]=0;
    file=fopen(target,"wb");CHECK(file!=0);if(!file)exit(1);
    CHECK(fwrite(bytes,1,n,file)==n);CHECK(fclose(file)==0);
}
static char original_interfaces[16384],original_resolver[16384];
static size_t read_fixture(const char *path,char *out)
{
    FILE *file=fopen(path,"rb");size_t n;CHECK(file!=0);if(!file)exit(1);
    n=fread(out,1,16383U,file);CHECK(feof(file)&&!ferror(file));fclose(file);out[n]=0;return n;
}
static void preserved(unsigned int exact)
{
    char text[16384];const char *lan2,*tail,*found;size_t n=read_fixture(interfaces_path,text);
    if(exact)CHECK(n==strlen(original_interfaces)&&!memcmp(text,original_interfaces,n));
    else {
        lan2=strstr(original_interfaces,"iface eth1 inet static");CHECK(lan2!=0);
        CHECK(!memcmp(text,original_interfaces,(size_t)(lan2-original_interfaces)));
        tail=strstr(lan2,"broadcast 192.168.4.255");CHECK(tail!=0);tail=strchr(tail,'\n');CHECK(tail!=0);
        found=strstr(strstr(text,"iface eth1 inet static"),"broadcast 192.168.4.255");CHECK(found!=0);
        found=strchr(found,'\n');CHECK(found!=0);CHECK(!strcmp(tail,found));
    }
    read_fixture(resolver_path,text);CHECK(!strcmp(text,original_resolver));
}
static void setup(fixture_t *f)
{
    char root[]="/tmp/4vrs-lan2-XXXXXX";const char *bindings[8];unsigned int i;
    CHECK(mkdtemp(root)!=0);strcpy(directory,root);
    snprintf(interfaces_path,sizeof(interfaces_path),"%s/interfaces",directory);
    snprintf(resolver_path,sizeof(resolver_path),"%s/resolv",directory);
    snprintf(trace_path,sizeof(trace_path),"%s/supervisor.log",directory);
    text_file(interfaces_path,"auto eth0 eth1 eth2 lo\niface lo inet loopback\niface eth0 inet static\n address 10.0.2.13\n network 10.0.0.0\n netmask 255.255.240.0\n broadcast 10.0.2.255\n gateway 10.0.0.1\n dns-nameserver 10.0.0.1 10.0.0.3\niface eth1 inet static\n address 192.168.4.127\n network 192.168.4.0\n netmask 255.255.255.0\n broadcast 192.168.4.255\n# vendor retained\niface eth2 inet static\n address 192.168.5.127\n netmask 255.255.255.0\n up /vendor/retained-hook\n");
    text_file(resolver_path,"search local\nnameserver 10.0.0.1\n# retained between DNS lines\nnameserver 10.0.0.3\n");
    if(getenv("LAN2_BASELINE")){
        copy_fixture("etc__network__interfaces",interfaces_path);
        copy_fixture("etc__resolv.conf",resolver_path);
    }
    read_fixture(interfaces_path,original_interfaces);read_fixture(resolver_path,original_resolver);
    kernel=mmap(0,sizeof(*kernel),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);CHECK(kernel!=MAP_FAILED);
    memset(kernel,0,sizeof(*kernel));kernel->lan.present=kernel->lan.up=1;
    strcpy(kernel->lan.address,"192.168.4.127");strcpy(kernel->lan.netmask,"255.255.255.0");strcpy(kernel->lan.broadcast,"192.168.4.255");
    kernel->all.lan[1]=kernel->lan;kernel->all.lan[0].present=kernel->all.lan[0].up=1;
    strcpy(kernel->all.lan[0].address,"10.0.2.13");strcpy(kernel->all.lan[0].netmask,"255.255.240.0");
    strcpy(kernel->all.lan[0].broadcast,"10.0.2.255");
    kernel->all.default_lan=kernel->all.default_routes=1;strcpy(kernel->all.gateway,"10.0.0.1");
    strcpy(kernel->all.dns[0],"10.0.0.1");strcpy(kernel->all.dns[1],"10.0.0.3");
    if(getenv("MOXA2_DNS_BASELINE")){
        strcpy(kernel->all.lan[0].address,"10.0.2.15");
        strcpy(kernel->lan.address,"192.168.3.127");strcpy(kernel->lan.broadcast,"192.168.3.255");
        kernel->all.lan[1]=kernel->lan;
    }
    memset(&environment,0,sizeof(environment));environment.store_directory=directory;
    environment.interfaces_path=interfaces_path;environment.resolver_path=resolver_path;
    environment.read_lan2=net_read;environment.write_lan2=net_write;environment.context=kernel;
    environment.clock=net_clock;environment.clock_context=kernel;
    environment.trace_path=trace_path;
    if(use_full){environment.read_network=full_read;environment.write_network=full_write;}
    if(use_service){
        CHECK(prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0)==0);
        memset(&service_environment,0,sizeof(service_environment));service_environment.directory=directory;
        service_environment.observe=full_read;service_environment.write=full_write;service_environment.client_ops=&dhcp_ops;
        service_environment.context=kernel;service_environment.clock=net_clock;service_environment.started_guardian=&kernel->guardian_pid;
        environment.service=&service_environment;kernel->dhcp_replies=1;
        kernel->all.lan[0].link=kernel->all.lan[1].link=1;
        kernel->all.lan[0].mac[0]=kernel->all.lan[1].mac[0]=2;kernel->all.lan[0].mac[5]=1;kernel->all.lan[1].mac[5]=2;
    }
    prepare(f,GATEWAY_CONFIG_OK,GATEWAY_CONFIG_SOURCE_ACTIVE);
    if(use_canonical){char path[1024];
        snprintf(path,sizeof(path),"%s/var__hda__4vrs__config__gateway.conf",getenv("LAN2_BASELINE"));
        CHECK(gateway_config_load_file(path,&f->config)==GATEWAY_CONFIG_OK);
    }else strcpy(f->config.ports[2].bind_address,"192.168.4.127");
    if(use_full&&!use_canonical)strcpy(f->config.ports[3].bind_address,"10.0.2.13");
    for(i=0;i<8U;++i)bindings[i]=f->config.ports[i].bind_address;
    {char path[256];int lock=gateway_network_store_lock(directory);CHECK(lock>=0);gateway_network_store_unlock(lock);
        snprintf(path,sizeof(path),"%s/gateway-network-recovery",directory);text_file(path,"preserved helper fixture\n");
        snprintf(path,sizeof(path),"%s/vendor-networking",directory);text_file(path,"preserved vendor fixture\n");}
    if(getenv("R7_RETAINED")){char path[256];
        snprintf(path,sizeof(path),"%s/confirmed",directory);copy_fixture("etc__4vrs-network__confirmed",path);
        snprintf(path,sizeof(path),"%s/good",directory);copy_fixture("etc__4vrs-network__good",path);
    }else {const char *stage=0;int result=gateway_network_enroll_detailed(&environment,bindings,&stage);
        printf("enroll result=%d stage=%s\n",result,stage);
        if(getenv("EXPECT_DNS_IMPORT_REJECT")){char path[256];
            CHECK(result<0&&!strcmp(stage,"import"));CHECK(kernel->writes==0);preserved(1);
            snprintf(path,sizeof(path),"%s/confirmed",directory);CHECK(access(path,F_OK)<0);
            snprintf(path,sizeof(path),"%s/good",directory);CHECK(access(path,F_OK)<0);
            printf("expected original DNS import refusal: %u checks, %u failed\n",checks,failed);
            exit(failed?1:0);
        }
        CHECK(result==0);if(result)exit(1);}
    {char before[16384],after[16384],confirmed[16384],good[16384],path[256];FILE *file;size_t n,m,cn,gn;
        snprintf(path,sizeof(path),"%s/confirmed",directory);cn=read_fixture(path,confirmed);
        snprintf(path,sizeof(path),"%s/good",directory);gn=read_fixture(path,good);
        file=fopen(interfaces_path,"rb");n=fread(before,1,sizeof(before),file);fclose(file);
        {const char *stage=0;int result=gateway_network_boot_restore_detailed(&environment,&stage);
        printf("baseline boot result=%d stage=%s\n",result,stage);CHECK(result==0);if(result)exit(1);}
        file=fopen(interfaces_path,"rb");m=fread(after,1,sizeof(after),file);fclose(file);
        CHECK(n==m&&!memcmp(before,after,n));
        snprintf(path,sizeof(path),"%s/confirmed",directory);m=read_fixture(path,after);CHECK(m==cn&&!memcmp(after,confirmed,m));
        snprintf(path,sizeof(path),"%s/good",directory);m=read_fixture(path,after);CHECK(m==gn&&!memcmp(after,good,m));}

    {char good[32768],confirmed[32768];size_t gn,cn;
        CHECK(gateway_network_store_read(directory,"confirmed",confirmed,sizeof(confirmed),&cn)==GATEWAY_NET_STORE_OK);
        CHECK(gateway_network_store_read(directory,"good",good,sizeof(good),&gn)==GATEWAY_NET_STORE_OK);
        if(!getenv("R7_RETAINED"))CHECK(cn==gn&&!memcmp(good,confirmed,cn));}
    if(use_boot_path){gateway_network_service_status_t state;unsigned int n;
        /* Cold kernel after persisted documents/store have survived reboot. */
        for(n=0;n<2U;++n){kernel->all.lan[n].up=0;kernel->all.lan[n].address[0]=0;
            kernel->all.lan[n].netmask[0]=kernel->all.lan[n].broadcast[0]=0;}
        kernel->all.default_lan=kernel->all.default_routes=0;kernel->all.gateway[0]=0;
        CHECK(gateway_network_boot_restore(&environment)==0);
        if(boot_lo_state==1){
            CHECK(gateway_network_vendor_boot_with_ops(directory,1,&boot_ops,0)<0);
            CHECK(!boot_lo); /* Warm r13 migration has no proof of hooks. */
            CHECK(!gateway_network_vendor_boot_with_ops(directory,0,&boot_ops,0));
            CHECK(boot_lo_state==3&&!boot_receipt_valid);boot_lo=0;
        }
        CHECK(!gateway_network_vendor_boot_with_ops(directory,1,&boot_ops,0));
        CHECK(boot_lo==1U&&!boot_eth2_calls);
        CHECK(!gateway_network_boot_service_with_environment(&service_environment));
        for(n=0;n<500U;++n){kernel->now+=10U;usleep(1000);
            if(!gateway_network_service_query(&service_environment,&state)&&state.ready)break;}
        CHECK(n<500U&&state.settled);
        CHECK(!strcmp(kernel->all.lan[0].address,getenv("MOXA2_DNS_BASELINE")?"10.0.2.15":"10.0.2.13")&&
              !strcmp(kernel->all.lan[1].address,getenv("MOXA2_DNS_BASELINE")?"192.168.3.127":"192.168.4.127"));
        CHECK(kernel->all.lan[0].up&&kernel->all.lan[1].up);
        boot_present_eth2=1;
        CHECK(!gateway_network_vendor_boot_with_ops(directory,1,&boot_ops,0));
        CHECK(boot_lo==1U&&boot_eth2_calls==1U); /* Optional device appears later; lo not reset. */
    }
    f->dependencies.network_environment=&environment;
    CHECK(gateway_application_init(&f->application,&f->dependencies)==0);
    step_to_runtime(f);CHECK(f->application.network.available);
}
static void tick(fixture_t *f)
{if(environment.service)kernel->now=f->now;gateway_application_step(&f->application);++f->now;usleep(1000);}
static void until(fixture_t *f,gateway_network_state_t state)
{
    unsigned int i;for(i=0;i<(environment.service?20000U:3000U)&&f->application.network.status.state!=state;++i)tick(f);
    if(f->application.network.status.state!=state)printf("network expected=%s got=%s\n",gateway_network_state_name(state),gateway_network_state_name(f->application.network.status.state));
    if(f->application.network.status.state!=state){char trace[16384];read_fixture(trace_path,trace);printf("trace: %s\n",trace);}
    CHECK(f->application.network.status.state==state);
}
static void reap(fixture_t *f)
{unsigned int i;for(i=0;i<1000U&&f->application.network.pid;++i)tick(f);CHECK(!f->application.network.pid);}
static void cleanup(fixture_t *f)
{
    static const char *names[]={"confirmed","good","candidate","write.tmp","lock","interfaces","resolv","gateway-network-recovery","vendor-networking","supervisor.log","owner.sock","owner.lock","commit.guard"};
    unsigned int i;char path[512];
    gateway_application_request_stop(&f->application);reap(f);step_to_stop(f);
    if(kernel->guardian_pid){int status;pid_t pid=0;CHECK(kill(kernel->guardian_pid,SIGTERM)==0);
        for(i=0;i<3000U;++i){pid=waitpid(kernel->guardian_pid,&status,WNOHANG);if(pid)break;usleep(1000);}
        CHECK(pid==kernel->guardian_pid&&WIFEXITED(status)&&!WEXITSTATUS(status));
        CHECK(prctl(PR_SET_CHILD_SUBREAPER,0,0,0,0)==0);
    }
    for(i=0;i<sizeof(names)/sizeof(names[0]);++i){snprintf(path,sizeof(path),"%s/%s",directory,names[i]);unlink(path);}
    CHECK(rmdir(directory)==0);munmap(kernel,sizeof(*kernel));
}
static gateway_network_settings_t candidate(fixture_t *f)
{gateway_network_settings_t s=f->application.network.confirmed.settings;strcpy(s.lan[1].address,"192.168.4.126");return s;}
static void transactions(void)
{
    fixture_t f;gateway_network_settings_t s;gateway_persistent_config_t config;unsigned int starts;
    setup(&f);s=candidate(&f);starts=f.transport[0].starts;
    CHECK(gateway_application_network_apply(&f.application,&s)==0);
    CHECK(gateway_application_network_keep(&f.application)!=0);
    CHECK(gateway_application_request_configuration(&f.application,&f.config)!=0);
    until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.126"));
    CHECK(f.transport[0].starts==starts);CHECK(!strcmp(f.application.coordinator.controller.ports[0].current.bind_address,"0.0.0.0"));
    preserved(1);CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);preserved(1);
    CHECK(!strcmp(kernel->lan.address,"192.168.4.126"));
    {char orphan[512];snprintf(orphan,sizeof(orphan),"%s.4vrs.tmp",interfaces_path);text_file(orphan,"interrupted boot write");}
    CHECK(gateway_application_network_boot(&environment)==0);preserved(0);CHECK(gateway_application_network_boot(&environment)==0);
    {char text[2048];FILE *file=fopen(interfaces_path,"r");size_t n;CHECK(file!=0);
        n=fread(text,1,sizeof(text)-1U,file);text[n]=0;fclose(file);
        CHECK(strstr(text,"broadcast 10.0.2.255")!=0);CHECK(strstr(text,"address 192.168.5.127")!=0);}
    /* Ordinary config writes must preserve durable affinity, even after Keep. */
    config=f.application.coordinator.selected;config.settings.ntp_interval_hours=6;
    CHECK(gateway_application_request_configuration(&f.application,&config)==0);
    {unsigned int i;for(i=0;i<100U&&gateway_application_configuration_state(&f.application)!=GATEWAY_CONFIG_TX_SUCCEEDED;++i)tick(&f);}
    CHECK(!strcmp(f.config.ports[2].bind_address,"192.168.4.127"));
    gateway_application_request_stop(&f.application);step_to_stop(&f);
    CHECK(gateway_application_init(&f.application,&f.dependencies)==0);step_to_runtime(&f);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.126"));
    strcpy(s.lan[1].address,"192.168.4.125");CHECK(gateway_application_network_apply(&f.application,&s)==0);
    until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);CHECK(gateway_application_network_revert(&f.application)==0);
    until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);CHECK(!strcmp(kernel->lan.address,"192.168.4.126"));
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.126"));
    preserved(0);cleanup(&f);
}
static void faults(void)
{
    fixture_t f;gateway_network_settings_t s;
    setup(&f);s=candidate(&f);CHECK(gateway_application_network_apply(&f.application,&s)==0);
    until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);kernel->now+=GATEWAY_NETWORK_CONFIRM_MS+1;
    until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);CHECK(!strcmp(kernel->lan.address,"192.168.4.127"));
    kernel->fail_apply=1;CHECK(gateway_application_network_apply(&f.application,&s)==0);
    until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);kernel->fail_apply=0;
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    kernel->fail_restore=1;CHECK(gateway_application_network_revert(&f.application)==0);
    until(&f,GATEWAY_NETWORK_ROLLBACK_FAILED);reap(&f);
    CHECK(gateway_application_network_apply(&f.application,&s)!=0);cleanup(&f);
}
static void bounded_and_bindings(void)
{
    fixture_t f;gateway_network_settings_t s;unsigned int i;int fd;
    setup(&f);s=candidate(&f);
    f.application.coordinator.controller.ports[0].lifecycle=GATEWAY_PORT_DEGRADED;
    CHECK(gateway_application_network_apply(&f.application,&s)!=0);CHECK(kernel->writes==0);
    f.application.coordinator.controller.ports[0].lifecycle=GATEWAY_PORT_READY;
    fd=open("/dev/null",O_RDONLY);CHECK(fd>=0);kernel->sentinel=fcntl(fd,F_DUPFD,100);close(fd);CHECK(kernel->sentinel>=100);
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    {char path[128];struct stat st;
        snprintf(path,sizeof(path),"/proc/%d/fd/%d",f.application.network.pid,kernel->sentinel);
        CHECK(lstat(path,&st)<0&&errno==ENOENT); /* supervisor also dropped the application fd */}
    CHECK(!kernel->leaked_fd);CHECK(gateway_application_network_revert(&f.application)==0);
    until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);close(kernel->sentinel);kernel->sentinel=0;
    /* A stuck mutator cannot stall the supervisor's deadline. */
    kernel->hang_apply=1;CHECK(gateway_application_network_apply(&f.application,&s)==0);
    for(i=0;i<100U;++i)tick(&f);
    kernel->now+=GATEWAY_NETWORK_APPLY_MS+1;
    until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);kernel->hang_apply=0;
    /* Rebind failure never exposes Keep; the original endpoint is recovered. */
    f.transport[2].start_failures_remaining=1;
    CHECK(gateway_application_network_apply(&f.application,&s)==0);
    until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.127"));
    /* External LAN1 drift is not hidden by rendering the old profile over it. */
    text_file(interfaces_path,"auto eth0 eth1\niface eth0 inet static\n address 10.0.2.14\n netmask 255.255.240.0\n gateway 10.0.0.1\niface eth1 inet static\n address 192.168.4.127\n network 192.168.4.0\n netmask 255.255.255.0\n");
    i=kernel->writes;CHECK(gateway_application_network_apply(&f.application,&s)==0);
    until(&f,GATEWAY_NETWORK_FAILED);reap(&f);CHECK(kernel->writes==i);
    CHECK(f.application.network.status.state==GATEWAY_NETWORK_FAILED);
    CHECK(gateway_application_network_boot(&environment)!=0);cleanup(&f);
}
static void crash(unsigned int hung)
{
    fixture_t f;gateway_network_settings_t s;pid_t child,reaped;int status;unsigned int i,writes;
    char before[32768],after[32768],events[16384];size_t bn,an;core_tick_t departed;struct rusage usage;
    setup(&f);s=candidate(&f);
    CHECK(gateway_network_store_read(directory,"confirmed",before,sizeof(before),&bn)==GATEWAY_NET_STORE_OK);
    kernel->hang_apply=hung;
    /* Adopt the isolated supervisor on application death, allowing exact reaping
     * and CPU accounting. This is host test scaffolding, not production logic. */
    CHECK(prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0)==0);
    child=fork();CHECK(child>=0);
    if(!child){
        if(gateway_application_network_apply(&f.application,&s))_exit(2);
        if(hung){for(i=0;i<2000U&&!kernel->apply_pid;++i)tick(&f);if(!kernel->apply_pid)_exit(3);}
        else {until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);if(f.application.network.status.state!=GATEWAY_NETWORK_WAIT_CONFIRM)_exit(4);}
        kernel->supervisor_pid=f.application.network.pid;kill(getpid(),SIGKILL);_exit(1);
    }
    CHECK(waitpid(child,&status,0)==child);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL);
    departed=wall_ms();reaped=0;memset(&usage,0,sizeof(usage));
    for(i=0;i<2000U;++i){reaped=wait4(kernel->supervisor_pid,&status,WNOHANG,&usage);if(reaped!=0)break;usleep(1000);}
    CHECK(reaped==kernel->supervisor_pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    if(!reaped){kill(kernel->supervisor_pid,SIGKILL);waitpid(kernel->supervisor_pid,0,0);}
    CHECK((core_tick_t)(wall_ms()-departed)<1000U);
    /* Restore can race ahead of the parent's waitpid; allow that ordering. */
    CHECK(kernel->restore_pid>0&&(int32_t)(kernel->restore_started-departed)<1000);
    {unsigned long cpu=(unsigned long)(usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)*1000000UL+
        (unsigned long)usage.ru_utime.tv_usec+(unsigned long)usage.ru_stime.tv_usec;
        CHECK(cpu<500000UL);printf("application SIGKILL hung=%u completion_ms=%lu restore_delta_ms=%ld cpu_us=%lu\n",
            hung,(unsigned long)(wall_ms()-departed),(long)(int32_t)(kernel->restore_started-departed),cpu);}
    CHECK(kill(kernel->apply_pid,0)<0&&errno==ESRCH);CHECK(kill(kernel->restore_pid,0)<0&&errno==ESRCH);
    CHECK(prctl(PR_SET_CHILD_SUBREAPER,0,0,0,0)==0);
    CHECK(!strcmp(kernel->lan.address,"192.168.4.127"));
    CHECK(gateway_network_store_read(directory,"confirmed",after,sizeof(after),&an)==GATEWAY_NET_STORE_OK);
    CHECK(an==bn&&!memcmp(before,after,bn));
    CHECK(gateway_network_store_read(directory,"good",after,sizeof(after),&an)==GATEWAY_NET_STORE_OK);
    CHECK(an==bn&&!memcmp(before,after,bn));
    CHECK(gateway_network_store_read(directory,"candidate",after,sizeof(after),&an)==GATEWAY_NET_STORE_ABSENT);
    CHECK(read_fixture(trace_path,events)<GATEWAY_NETWORK_TRACE_LIMIT*160U);
    CHECK(strstr(events,"reason=1")!=0&&strstr(events,"event=8 ")!=0);
    CHECK(strstr(events,"192.168.")==0&&strstr(events,"10.0.")==0);
    printf("isolated supervisor events (hung=%u):\n%s",hung,events);
    writes=kernel->writes;usleep(100000);CHECK(kernel->writes==writes);CHECK(!strcmp(kernel->lan.address,"192.168.4.127"));
    preserved(1);
    /* Pending bytes can never win over confirmed at boot. */
    {char bytes[32768];size_t n;gateway_network_profile_t p=f.application.network.confirmed;
        CHECK(gateway_network_profile_candidate(&p,&s,&p)==0);
        CHECK(gateway_network_profile_encode(&p,bytes,sizeof(bytes),&n)==0);
        usleep(50000);CHECK(gateway_network_store_write(directory,"candidate",bytes,n)==GATEWAY_NET_STORE_OK);}
    CHECK(gateway_application_network_boot(&environment)==0);
    CHECK(gateway_network_runtime_init(&f.application.network,&environment)==0);
    CHECK(!strcmp(f.application.network.confirmed.settings.lan[1].address,"192.168.4.127"));
    cleanup(&f);
}
static int pending_key=-1;
static int panel_open(void *ctx){(void)ctx;return 0;}
static void panel_close(void *ctx){(void)ctx;}
static int panel_draw(void *ctx,const gateway_panel_screen_t *s)
{unsigned int i;(void)ctx;for(i=0;i<8U;++i)CHECK(strlen(s->row[i])==16U);return 0;}
static int panel_poll(void *ctx,unsigned int *k)
{(void)ctx;if(pending_key<0)return 0;*k=(unsigned int)pending_key;pending_key=-1;return 1;}
static const gateway_panel_ops_t panel_ops={panel_open,panel_open,panel_draw,panel_poll,panel_close,panel_close};
static void press(gateway_panel_t *p,fixture_t *f,unsigned int key)
{pending_key=(int)key;gateway_panel_step(p,&f->application);}
static void menu(void)
{
    fixture_t f;gateway_panel_t p;unsigned int i;setup(&f);
    CHECK(gateway_panel_init(&p,&panel_ops,0)==0);gateway_panel_step(&p,&f.application);
    press(&p,&f,GATEWAY_PANEL_KEY_F3); /* main menu */
    press(&p,&f,GATEWAY_PANEL_KEY_F4);press(&p,&f,GATEWAY_PANEL_KEY_F4);
    press(&p,&f,GATEWAY_PANEL_KEY_F3);CHECK(p.view==GATEWAY_PANEL_CONFIG_PORTS);
    press(&p,&f,GATEWAY_PANEL_KEY_F5);CHECK(p.view==GATEWAY_PANEL_NETWORK);
    press(&p,&f,GATEWAY_PANEL_KEY_F5);CHECK(p.view==GATEWAY_PANEL_NETWORK_EDIT);
    for(i=0;i<3U;++i)press(&p,&f,GATEWAY_PANEL_KEY_F3);
    press(&p,&f,GATEWAY_PANEL_KEY_F2);CHECK(!strcmp(p.lan2_candidate.lan[1].address,"192.168.4.126"));
    press(&p,&f,GATEWAY_PANEL_KEY_F1);CHECK(kernel->writes==0); /* Cancel */
    press(&p,&f,GATEWAY_PANEL_KEY_F5);for(i=0;i<3U;++i)press(&p,&f,GATEWAY_PANEL_KEY_F3);
    press(&p,&f,GATEWAY_PANEL_KEY_F2);press(&p,&f,GATEWAY_PANEL_KEY_F5);
    CHECK(p.view==GATEWAY_PANEL_NETWORK_CONFIRM);press(&p,&f,GATEWAY_PANEL_KEY_F3);
    CHECK(p.view==GATEWAY_PANEL_NETWORK_RESULT);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    gateway_panel_step(&p,&f.application);CHECK(strstr(p.next.row[5],"Keep")!=0);
    press(&p,&f,GATEWAY_PANEL_KEY_F3);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    gateway_panel_shutdown(&p);cleanup(&f);
}
static void graceful_stop(void)
{
    fixture_t f;gateway_network_settings_t s;setup(&f);s=candidate(&f);
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    gateway_application_request_stop(&f.application);reap(&f);step_to_stop(&f);
    CHECK(f.application.network.status.state==GATEWAY_NETWORK_REVERTED);
    CHECK(!strcmp(kernel->lan.address,"192.168.4.127"));CHECK(f.application.exit_status==GATEWAY_EXIT_CLEAN);
    cleanup(&f);
}
static void cold_boot_flow(unsigned int warm)
{
    fixture_t f;gateway_network_service_status_t before,after;unsigned int i;
    use_boot_path=use_full=use_service=1;boot_lo=boot_eth2_calls=boot_present_eth2=0;boot_lo_state=warm?1:0;boot_receipt_valid=0;
    setup(&f);use_boot_path=use_full=use_service=0;
    CHECK(!gateway_network_service_query(&service_environment,&before));
    for(i=0;i<300U;++i)tick(&f);
    CHECK(!gateway_network_service_query(&service_environment,&after));
    CHECK(before.owner_pid==after.owner_pid&&before.guardian_pid==after.guardian_pid&&after.ready);
    boot_present_eth2=0;
    CHECK(!gateway_network_boot_restore(&environment));
    CHECK(!gateway_network_vendor_boot_with_ops(directory,1,&boot_ops,0));
    CHECK(boot_lo==1U); /* start/start does not rerun completed lo hooks. */
    CHECK(!gateway_network_boot_service_with_environment(&service_environment));
    CHECK(!gateway_network_vendor_boot_with_ops(directory,0,&boot_ops,0));
    CHECK(boot_lo_state==3&&!boot_receipt_valid);
    CHECK(!gateway_network_vendor_boot_with_ops(directory,1,&boot_ops,0));
    CHECK(boot_lo==3U&&boot_lo_state==1&&boot_receipt_valid);
    CHECK(!gateway_network_boot_service_with_environment(&service_environment));
    CHECK(!gateway_network_service_query(&service_environment,&after));
    CHECK(before.owner_pid==after.owner_pid&&before.guardian_pid==after.guardian_pid);
    CHECK(f.application.coordinator.state==GATEWAY_APP_READY);preserved(1);
    printf("cold boot: documents -> vendor lo -> owner confirmed -> application adoption passed\n");
    cleanup(&f);
}
static void canonical_flow(void)
{
    fixture_t f;gateway_network_settings_t s;gateway_persistent_config_t expected;
    use_canonical=getenv("LAN2_BASELINE")?1U:0U;use_full=use_service=1;
    setup(&f);use_canonical=use_full=use_service=0;s=candidate(&f);
    CHECK(gateway_application_network_boot(&environment)==0);preserved(1);
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(gateway_application_network_revert(&f.application)==0);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);preserved(1);
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    CHECK(gateway_application_network_boot(&environment)==0);preserved(0);
    expected=f.application.coordinator.selected;
    gateway_application_request_stop(&f.application);step_to_stop(&f);
    CHECK(gateway_application_init(&f.application,&f.dependencies)==0);step_to_runtime(&f);
    CHECK(!memcmp(&f.application.coordinator.selected,&expected,sizeof(expected)));
    if(getenv("LAN2_BASELINE"))CHECK(!memcmp(&expected,&f.config,sizeof(f.config)));
    cleanup(&f);
}
static void invalid_networks(void)
{
    static const char *options[]={" network 192.168.9.0\n"," network 192.168.4.0\n network 192.168.4.0\n",
        " network garbage\n"," network 192.168.4.0 trailing\n"," up /never/execute\n"," metric 9\n"," gateway 192.168.4.1\n",""};
    fixture_t f;unsigned int i,j;const char *bindings[8],*stage;char text[2048];
    setup(&f);for(j=0;j<8U;++j)bindings[j]=f.config.ports[j].bind_address;
    CHECK(gateway_network_enroll_detailed(&environment,bindings,&stage)!=0&&!strcmp(stage,"persist"));
    for(i=0;i<sizeof(options)/sizeof(options[0]);++i){
        snprintf(text,sizeof(text),"auto eth0 eth1\niface eth0 inet static\n address 10.0.2.13\n netmask 255.255.240.0\n gateway 10.0.0.1\niface eth1 inet %s\n%s address 192.168.4.127\n netmask 255.255.255.0\n broadcast 192.168.4.255\n",i==7U?"dhcp":"static",options[i]);
        text_file(interfaces_path,text);
        CHECK(gateway_network_enroll_detailed(&environment,bindings,&stage)!=0);
        CHECK(!strcmp(stage,(i==4U||i==7U)?"static-scope":"import"));CHECK(kernel->writes==0);
        CHECK(gateway_application_network_boot(&environment)!=0);
    }
    {const char *boot_stage=0;CHECK(gateway_network_boot_restore_detailed(&environment,&boot_stage)!=0);
        CHECK(!strcmp(boot_stage,"documents-current"));}
    text_file(interfaces_path,original_interfaces);kernel->lan.present=0;
    CHECK(gateway_network_enroll_detailed(&environment,bindings,&stage)!=0&&!strcmp(stage,"observation-match"));
    kernel->lan.present=1;cleanup(&f);
}
static void boot_stage_failures(void)
{
    fixture_t f;gateway_network_profile_t p;char bytes[GATEWAY_NETWORK_SNAPSHOT_MAX];
    size_t n;const char *stage=0;
    use_full=1;setup(&f);use_full=0;p=f.application.network.confirmed;
    /* Hooks are preserved by the document model but may never be executed
     * for a product-owned LAN. A valid CRC is not production boot acceptance. */
    {char *at=strstr(p.interfaces,"iface eth1");size_t tail=strlen(at);
        memmove(at+strlen(" up /never/execute\n"),at,tail+1U);
        memcpy(at," up /never/execute\n",strlen(" up /never/execute\n"));}
    CHECK(gateway_network_profile_encode(&p,bytes,sizeof(bytes),&n)==0);
    CHECK(gateway_network_store_write(directory,"confirmed",bytes,n)==GATEWAY_NET_STORE_OK);
    CHECK(gateway_network_boot_restore_detailed(&environment,&stage)!=0);
    CHECK(!strcmp(stage,"profile-scope"));CHECK(kernel->writes==0);preserved(1);
    p=f.application.network.confirmed;
    p.settings.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;p.settings.automatic_dns=1;p.settings.dns_lan=2;
    {char *at=strstr(p.interfaces,"iface eth1 inet static"),*tail=strstr(at,"# vendor retained");
        memmove(at+strlen("iface eth1 inet dhcp\n"),tail,strlen(tail)+1U);
        memcpy(at,"iface eth1 inet dhcp\n",strlen("iface eth1 inet dhcp\n"));}
    CHECK(gateway_network_profile_encode(&p,bytes,sizeof(bytes),&n)==0);
    CHECK(gateway_network_store_write(directory,"confirmed",bytes,n)==GATEWAY_NET_STORE_OK);
    CHECK(gateway_network_boot_restore_detailed(&environment,&stage)!=0&&!strcmp(stage,"profile-scope"));
    CHECK(kernel->writes==0);preserved(1);
    p=f.application.network.confirmed;
    CHECK(gateway_network_profile_encode(&p,bytes,sizeof(bytes),&n)==0);
    CHECK(gateway_network_store_write(directory,"confirmed",bytes,n)==GATEWAY_NET_STORE_OK);
    text_file(resolver_path,"nameserver 10.0.0.9\n");
    CHECK(gateway_network_boot_restore_detailed(&environment,&stage)!=0);
    CHECK(!strcmp(stage,"documents-current"));CHECK(kernel->writes==0);
    text_file(resolver_path,original_resolver);
    CHECK(gateway_network_boot_restore_detailed(&environment,&stage)==0&&!strcmp(stage,"ok"));
    preserved(1);cleanup(&f);
}
static void renewal_transaction(unsigned int mode)
{
    fixture_t f;gateway_network_settings_t s;unsigned int i,writes;core_tick_t deadline,expiry;
    gateway_network_service_status_t status;
    char confirmed[16384],good[16384],path[256],current[16384];size_t cn,gn,n;
    use_full=use_service=1;setup(&f);use_full=use_service=0;kernel->bench=1;
    snprintf(path,sizeof(path),"%s/confirmed",directory);cn=read_fixture(path,confirmed);
    snprintf(path,sizeof(path),"%s/good",directory);gn=read_fixture(path,good);
    s=f.application.network.confirmed.settings;s.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(kernel->all.lan[1].address,"192.168.4.126"));
    CHECK(gateway_network_service_query(&service_environment,&status)==0&&status.ready&&status.lease_valid[1]);
    expiry=status.lease_expiry[1];deadline=f.application.network.status.deadline;writes=kernel->writes;
    if(mode)kernel->dhcp_replies=0;
    f.now=expiry-30000U;for(i=0;i<600U;++i)tick(&f);
    CHECK(kernel->renew_sent);
    CHECK(f.application.network.status.state==GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(kernel->writes==writes&&f.application.network.status.deadline==deadline);
    CHECK(!strcmp(kernel->all.lan[1].address,"192.168.4.126"));
    CHECK(gateway_network_service_query(&service_environment,&status)==0&&status.ready&&status.lease_valid[1]);
    if(!mode){CHECK(kernel->renew_received&&status.lease_expiry[1]>expiry);
        /* A renewal does not restart or shorten the independent 60s countdown. */
        f.now=deadline-20U;for(i=0;i<10U;++i)tick(&f);
        CHECK(f.application.network.status.state==GATEWAY_NETWORK_WAIT_CONFIRM);
        f.now=deadline+1U;until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);preserved(1);
    }else if(mode==3U){
        CHECK(gateway_application_network_revert(&f.application)==0);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
        for(i=0;i<300U;++i)tick(&f);
        writes=kernel->writes;kernel->dhcp_replies=1;f.now=expiry+1000U;
        for(i=0;i<300U;++i)tick(&f);
        CHECK(kernel->writes==writes);preserved(1);
    }else {
        CHECK(status.dhcp_state[1]==GATEWAY_DHCP_RENEWING);
        f.now=expiry-8000U;for(i=0;i<400U;++i)tick(&f);
        CHECK(f.application.network.status.state==GATEWAY_NETWORK_WAIT_CONFIRM);
        CHECK(gateway_network_service_query(&service_environment,&status)==0&&status.ready&&status.lease_valid[1]);
        CHECK(status.dhcp_state[1]==GATEWAY_DHCP_REBINDING);CHECK(kernel->writes==writes);
        if(mode==1U){
            kernel->dhcp_replies=1;f.now=expiry-1000U;for(i=0;i<500U;++i)tick(&f);
            CHECK(gateway_network_service_query(&service_environment,&status)==0&&status.ready&&status.lease_valid[1]);
            CHECK(status.lease_expiry[1]>expiry&&kernel->renew_received);
            CHECK(f.application.network.status.state==GATEWAY_NETWORK_WAIT_CONFIRM&&kernel->writes==writes);
            CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
            kernel->dhcp_replies=0;f.now=status.lease_expiry[1]+1U;for(i=0;i<800U;++i)tick(&f);
            CHECK(!kernel->all.lan[1].address[0]);
            CHECK(f.application.coordinator.controller.ports[2].lifecycle==GATEWAY_PORT_NETWORK_WAIT);
            kernel->dhcp_replies=1;for(i=0;i<14000U&&strcmp(kernel->all.lan[1].address,"192.168.4.126");++i)tick(&f);
            CHECK(!strcmp(kernel->all.lan[1].address,"192.168.4.126"));
        }else{
            f.now=expiry+1U;until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);preserved(1);
        }
    }
    CHECK(kernel->all.default_lan==1&&!strcmp(kernel->all.gateway,"10.0.0.1"));
    CHECK(!strcmp(kernel->all.dns[0],"10.0.0.1")&&!strcmp(kernel->all.dns[1],"10.0.0.3"));
    {char trace[16384];read_fixture(trace_path,trace);CHECK(strstr(trace,"event=12 ")!=0);CHECK(strstr(trace,"event=14 ")!=0);
        if(mode==0U)CHECK(strstr(trace,"state=6 reason=3 ")!=0);
        if(mode==2U)CHECK(strstr(trace,"state=6 reason=6 ")!=0);
    }
    if(mode!=1U){
        snprintf(path,sizeof(path),"%s/confirmed",directory);n=read_fixture(path,current);CHECK(n==cn&&!memcmp(current,confirmed,n));
        snprintf(path,sizeof(path),"%s/good",directory);n=read_fixture(path,current);CHECK(n==gn&&!memcmp(current,good,n));
    }
    printf("bench renewal mode=%u sent=%u received=%u final_state=%u\n",mode,kernel->renew_sent,kernel->renew_received,(unsigned int)f.application.network.status.state);
    cleanup(&f);
}
static void renewal_transactions(void)
{unsigned int mode;for(mode=0;mode<4U;++mode)renewal_transaction(mode);}
static void service_transactions(void)
{
    fixture_t f;gateway_network_settings_t s;unsigned int i,uart_opens;
    use_full=use_service=1;setup(&f);use_full=use_service=0;s=candidate(&f);
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(gateway_application_network_revert(&f.application)==0);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
    s=f.application.network.confirmed.settings;s.lan[1].mode=GATEWAY_LAN_DHCP_CLIENT;s.default_lan=1;s.automatic_dns=1;s.dns_lan=2;
    uart_opens=f.backend[2].opens;
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.100"));
    CHECK(f.backend[2].opens==uart_opens&&kernel->all.default_lan==1&&!strcmp(kernel->all.dns[0],"192.168.4.1"));
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    for(i=0;i<100U;++i)tick(&f);
    s=f.application.network.confirmed.settings;s.lan[0].mode=GATEWAY_LAN_DHCP_CLIENT;s.default_lan=2;
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(f.application.coordinator.controller.ports[3].current.bind_address,"10.0.2.100"));
    CHECK(kernel->dhcp_opens[1]==1U&&kernel->dhcp_opens[0]==1U);
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    for(i=0;i<100U;++i)tick(&f);
    CHECK(gateway_application_network_boot(&environment)==0);
    gateway_application_request_stop(&f.application);step_to_stop(&f);
    CHECK(gateway_application_init(&f.application,&f.dependencies)==0);step_to_runtime(&f);
    for(i=0;i<3000U&&strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.100");++i)tick(&f);
    CHECK(f.application.coordinator.controller.ports[2].lifecycle==GATEWAY_PORT_READY);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.100"));
    kernel->dhcp_replies=0;f.now=f.application.network.lease_expiry[1]+1U;
    for(i=0;i<2000U&&kernel->all.lan[1].address[0];++i)tick(&f);
    CHECK(!kernel->all.lan[1].address[0]&&!kernel->all.default_lan&&!kernel->all.dns[0][0]);
    for(i=0;i<30U;++i)tick(&f);
    CHECK(f.application.coordinator.controller.ports[2].lifecycle==GATEWAY_PORT_NETWORK_WAIT);
    CHECK(f.application.coordinator.controller.ports[0].lifecycle==GATEWAY_PORT_READY);
    s=f.application.network.confirmed.settings;s.lan[1].mode=s.lan[0].mode=GATEWAY_LAN_STATIC;
    s.default_lan=1;s.automatic_dns=0;s.dns_lan=0;strcpy(s.dns[0],"10.0.0.1");strcpy(s.dns[1],"10.0.0.3");
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.127"));
    CHECK(!gateway_application_network_revert(&f.application));until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
    CHECK(f.application.coordinator.controller.ports[2].lifecycle==GATEWAY_PORT_NETWORK_WAIT);
    CHECK(!kernel->all.lan[1].address[0]); /* DHCP policy restored, expired lease never resurrected. */
    CHECK(!gateway_application_network_apply(&f.application,&s));until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    cleanup(&f);
}

static void binding_edits(void)
{
    fixture_t f;gateway_persistent_config_t c;gateway_network_settings_t s;unsigned int i;
    use_full=1;setup(&f);use_full=0;s=candidate(&f);
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    c=f.application.coordinator.selected;strcpy(c.ports[2].bind_address,"0.0.0.0");++c.ports[2].revision;
    f.stage_result=GATEWAY_CONFIG_IO_ERROR;CHECK(gateway_application_request_configuration(&f.application,&c)!=0);
    CHECK(!strcmp(f.application.coordinator.selected.ports[2].bind_address,"192.168.4.126"));
    f.stage_result=GATEWAY_CONFIG_OK;CHECK(gateway_application_request_configuration(&f.application,&c)==0);
    for(i=0;i<100U&&gateway_application_configuration_state(&f.application)!=GATEWAY_CONFIG_TX_SUCCEEDED;++i)tick(&f);
    CHECK(gateway_application_configuration_state(&f.application)==GATEWAY_CONFIG_TX_SUCCEEDED);
    CHECK(f.config.ports[2].bind_policy==1U);for(i=0;i<10U;++i)tick(&f);
    CHECK(!strcmp(f.application.coordinator.selected.ports[2].bind_address,"0.0.0.0"));
    strcpy(s.lan[1].address,"192.168.4.125");
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"0.0.0.0"));
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    c=f.application.coordinator.selected;strcpy(c.ports[2].bind_address,"10.0.2.13");++c.ports[2].revision;
    CHECK(gateway_application_request_configuration(&f.application,&c)==0);
    for(i=0;i<100U&&gateway_application_configuration_state(&f.application)!=GATEWAY_CONFIG_TX_SUCCEEDED;++i)tick(&f);
    CHECK(f.config.ports[2].bind_policy==2U);
    strcpy(s.lan[0].address,"10.0.2.14");
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"10.0.2.14"));
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    gateway_application_request_stop(&f.application);step_to_stop(&f);
    CHECK(gateway_application_init(&f.application,&f.dependencies)==0);step_to_runtime(&f);
    CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"10.0.2.14"));
    cleanup(&f);
}

static void full_transactions(void)
{
    fixture_t f;gateway_network_settings_t s;gateway_network_observation_t original;
    char bytes[16384];unsigned int starts;use_full=1;setup(&f);use_full=0;
    original=kernel->all;starts=f.transport[0].starts;
    s=candidate(&f);strcpy(s.lan[0].address,"10.0.2.14");
    s.default_lan=2;strcpy(s.lan[1].gateway,"192.168.4.1");s.lan[0].gateway[0]=0;
    strcpy(s.dns[0],"192.168.4.1");s.dns[1][0]=0;
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!strcmp(kernel->all.lan[0].address,"10.0.2.14")&&kernel->all.default_lan==2);
    CHECK(!strcmp(f.application.coordinator.controller.ports[3].current.bind_address,"10.0.2.14"));
    CHECK(f.transport[0].starts==starts);
    CHECK(gateway_application_network_revert(&f.application)==0);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
    CHECK(!memcmp(&kernel->all,&original,sizeof(original)));preserved(1);
    CHECK(!strcmp(f.application.coordinator.controller.ports[3].current.bind_address,"10.0.2.13"));
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(gateway_application_network_keep(&f.application)==0);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    CHECK(gateway_application_network_boot(&environment)==0);
    read_fixture(interfaces_path,bytes);CHECK(strstr(bytes,"address 10.0.2.14")!=0);
    CHECK(!strcmp(strstr(bytes,"# vendor retained"),strstr(original_interfaces,"# vendor retained")));
    CHECK(gateway_application_network_boot(&environment)==0);
    /* A later canceled transaction may change both route and resolver, but
     * restoration must target the new durable generation. */
    s=f.application.network.confirmed.settings;s.default_lan=0;s.lan[1].gateway[0]=0;
    strcpy(s.dns[0],"10.0.0.3");
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    kernel->now+=60001U;until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
    CHECK(kernel->all.default_lan==2&&!strcmp(kernel->all.dns[0],"192.168.4.1"));
    CHECK(!strcmp(kernel->all.lan[0].address,"10.0.2.14"));
    /* Crash/boot can leave the candidate resolver installed, but it must
     * never authorize candidate promotion. No live process is running here. */
    {gateway_network_profile_t p;char data[32768];size_t size;
        CHECK(gateway_network_profile_candidate(&f.application.network.confirmed,&s,&p)==0);
        CHECK(gateway_network_profile_encode(&p,data,sizeof(data),&size)==0);
        CHECK(gateway_network_store_write(directory,"candidate",data,size)==GATEWAY_NET_STORE_OK);
        text_file(resolver_path,p.resolver);CHECK(gateway_application_network_boot(&environment)==0);
        read_fixture(resolver_path,bytes);CHECK(!strcmp(bytes,f.application.network.confirmed.resolver));
    }
    CHECK(gateway_application_network_apply(&f.application,&s)==0);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    kernel->fail_store_sync=1;CHECK(gateway_application_network_keep(&f.application)==0);
    until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
    CHECK(kernel->all.default_lan==2&&!strcmp(kernel->all.dns[0],"192.168.4.1"));
    cleanup(&f);
}

static void interrupted_keep(void)
{
 unsigned int at;
 for(at=1;at<=6U;at+=5U){fixture_t f;gateway_network_settings_t s;gateway_network_profile_t before,after;char bytes[32768];size_t n;unsigned int i;int writer;
  use_full=use_service=1;setup(&f);before=f.application.network.confirmed;s=candidate(&f);
  CHECK(!gateway_application_network_apply(&f.application,&s));until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
  kernel->hang_sync_after=at;CHECK(!gateway_application_network_keep(&f.application));
  for(i=0;i<3000U&&!kernel->sync_pid;++i)tick(&f);
  CHECK(kernel->sync_pid>0);writer=kernel->sync_pid;
  gateway_network_runtime_disconnect(&f.application.network);reap(&f);
  CHECK(!strcmp(kernel->all.lan[1].address,"192.168.4.127"));
  CHECK(!gateway_network_store_boot(directory,bytes,sizeof(bytes),&n));CHECK(!gateway_network_profile_decode(bytes,n,&after));
  CHECK(!memcmp(&before,&after,sizeof(before)));CHECK(kill(writer,0)<0&&errno==ESRCH);
  kernel->hang_sync_after=0;cleanup(&f);use_full=use_service=0;
 }
}

static void menu_full(void)
{
 fixture_t f;gateway_panel_t p;unsigned int i;use_full=use_service=1;setup(&f);
 CHECK(!gateway_panel_init(&p,&panel_ops,0));gateway_panel_step(&p,&f.application);
 press(&p,&f,GATEWAY_PANEL_KEY_F3);press(&p,&f,GATEWAY_PANEL_KEY_F4);press(&p,&f,GATEWAY_PANEL_KEY_F4);
 press(&p,&f,GATEWAY_PANEL_KEY_F3);press(&p,&f,GATEWAY_PANEL_KEY_F5);
 /* LAN1 address -> application Apply -> Revert via the real menu. */
 press(&p,&f,GATEWAY_PANEL_KEY_F4);press(&p,&f,GATEWAY_PANEL_KEY_F5);
 for(i=0;i<3U;++i)press(&p,&f,GATEWAY_PANEL_KEY_F3);
 press(&p,&f,GATEWAY_PANEL_KEY_F4);press(&p,&f,GATEWAY_PANEL_KEY_F5);press(&p,&f,GATEWAY_PANEL_KEY_F3);
 until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);CHECK(!strcmp(kernel->all.lan[0].address,"10.0.2.14"));
 press(&p,&f,GATEWAY_PANEL_KEY_F1);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);press(&p,&f,GATEWAY_PANEL_KEY_F1);
 /* No default route, then restore. */
 press(&p,&f,GATEWAY_PANEL_KEY_F4);press(&p,&f,GATEWAY_PANEL_KEY_F5);press(&p,&f,GATEWAY_PANEL_KEY_F2);
 press(&p,&f,GATEWAY_PANEL_KEY_F5);press(&p,&f,GATEWAY_PANEL_KEY_F3);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);CHECK(!kernel->all.default_lan);
 press(&p,&f,GATEWAY_PANEL_KEY_F1);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);press(&p,&f,GATEWAY_PANEL_KEY_F1);
 /* LAN2 DHCP selection and Keep, without changing route/DNS. */
 press(&p,&f,GATEWAY_PANEL_KEY_F2);press(&p,&f,GATEWAY_PANEL_KEY_F2);press(&p,&f,GATEWAY_PANEL_KEY_F5);
 for(i=0;i<8U;++i)press(&p,&f,GATEWAY_PANEL_KEY_F3);
 press(&p,&f,GATEWAY_PANEL_KEY_F4);press(&p,&f,GATEWAY_PANEL_KEY_F5);press(&p,&f,GATEWAY_PANEL_KEY_F3);
 until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);press(&p,&f,GATEWAY_PANEL_KEY_F3);until(&f,GATEWAY_NETWORK_KEPT);reap(&f);press(&p,&f,GATEWAY_PANEL_KEY_F1);
 for(i=0;i<3U;++i)press(&p,&f,GATEWAY_PANEL_KEY_F4);
 press(&p,&f,GATEWAY_PANEL_KEY_F5);for(i=0;i<8U;++i)press(&p,&f,GATEWAY_PANEL_KEY_F3);
 press(&p,&f,GATEWAY_PANEL_KEY_F2);CHECK(p.lan2_candidate.dns_lan==2);
 press(&p,&f,GATEWAY_PANEL_KEY_F5);press(&p,&f,GATEWAY_PANEL_KEY_F3);until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
 CHECK(!strcmp(kernel->all.dns[0],"192.168.4.1")&&kernel->all.default_lan==1);
 press(&p,&f,GATEWAY_PANEL_KEY_F1);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);press(&p,&f,GATEWAY_PANEL_KEY_F1);
 press(&p,&f,GATEWAY_PANEL_KEY_F4);press(&p,&f,GATEWAY_PANEL_KEY_F4);
 CHECK(!strcmp(p.next.row[5],"DHCP bound      ")||strstr(p.next.row[5],"DHCP bound")!=0);
 CHECK(strstr(p.next.row[6],"Lease ")!=0);
 gateway_panel_shutdown(&p);cleanup(&f);use_full=use_service=0;
}

static void transaction_supervisor_death(void)
{
 fixture_t f;gateway_network_settings_t s;gateway_network_profile_t before,after;char bytes[32768];size_t n;unsigned int i;
 use_full=use_service=1;setup(&f);before=f.application.network.confirmed;s=candidate(&f);
 CHECK(!gateway_application_network_apply(&f.application,&s));until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
 CHECK(kill(f.application.network.pid,SIGKILL)==0);until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
 CHECK(!strcmp(f.application.coordinator.controller.ports[2].current.bind_address,"192.168.4.127"));
 for(i=0;i<1500U&&strcmp(kernel->all.lan[1].address,"192.168.4.127");++i)tick(&f);
 CHECK(i<1500U);CHECK(!gateway_network_store_boot(directory,bytes,sizeof(bytes),&n));
 CHECK(!gateway_network_profile_decode(bytes,n,&after)&&!memcmp(&before,&after,sizeof(before)));
 /* The persistent owner recovers even when the transaction supervisor dies.
  * Its unconfirmed record remains non-bootable; the next transaction replaces it. */
 CHECK(!gateway_application_network_apply(&f.application,&s));until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
 CHECK(!gateway_application_network_revert(&f.application));until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);
 cleanup(&f);use_full=use_service=0;
}

/* External immutable #2 archive, or a reviewed single-key normalized copy.
 * No product parser bypass: setup uses production config load/enroll/boot. */
static void moxa2_dns_flow(void)
{
    fixture_t f;gateway_network_settings_t s;gateway_network_service_status_t before,after;
    gateway_persistent_config_t config;char bytes[16384],expected[16384],*at;
    unsigned int starts;size_t n;
    use_canonical=use_full=use_service=use_boot_path=1;
    boot_lo=boot_eth2_calls=boot_present_eth2=0;boot_lo_state=1;boot_receipt_valid=0;
    setup(&f);config=f.config;starts=f.transport[0].starts;boot_present_eth2=0;
    CHECK(!strcmp(config.ports[0].bind_address,"10.0.2.15"));
    CHECK(f.application.network.confirmed.affinity[0]==1);
    CHECK(!memcmp(&f.application.coordinator.selected,&config,sizeof(config)));
    CHECK(!gateway_network_service_query(&service_environment,&before));
    CHECK(!gateway_network_vendor_boot_with_ops(directory,1,&boot_ops,0));
    CHECK(!gateway_network_vendor_boot_with_ops(directory,0,&boot_ops,0));
    CHECK(boot_lo_state==3);
    CHECK(!gateway_network_vendor_boot_with_ops(directory,1,&boot_ops,0));
    CHECK(!gateway_network_boot_service_with_environment(&service_environment));
    CHECK(!gateway_network_service_query(&service_environment,&after));
    CHECK(before.owner_pid==after.owner_pid&&before.guardian_pid==after.guardian_pid);
    preserved(1);s=f.application.network.confirmed.settings;
    strcpy(s.lan[1].address,"192.168.3.126");
    CHECK(!gateway_application_network_apply(&f.application,&s));until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!gateway_application_network_revert(&f.application));until(&f,GATEWAY_NETWORK_REVERTED);reap(&f);preserved(1);
    CHECK(!strcmp(kernel->all.lan[1].address,"192.168.3.127"));
    CHECK(!gateway_application_network_apply(&f.application,&s));until(&f,GATEWAY_NETWORK_WAIT_CONFIRM);
    CHECK(!gateway_application_network_keep(&f.application));until(&f,GATEWAY_NETWORK_KEPT);reap(&f);
    CHECK(!gateway_application_network_boot(&environment));
    /* Only the explicitly edited LAN2 stanza may be rendered. */
    read_fixture(interfaces_path,bytes);at=strstr(original_interfaces,"iface eth1 inet static");CHECK(at!=0);
    CHECK(!memcmp(bytes,original_interfaces,(size_t)(at-original_interfaces)));
    CHECK(strstr(bytes,"address 192.168.3.126")!=0);
    CHECK(!strcmp(strstr(bytes,"iface eth2"),strstr(original_interfaces,"iface eth2")));
    read_fixture(resolver_path,bytes);CHECK(!strcmp(bytes,original_resolver));
    CHECK(!strcmp(kernel->all.lan[0].broadcast,"10.0.2.255"));
    CHECK(f.transport[0].starts==starts);
    CHECK(!memcmp(&f.application.coordinator.selected,&config,sizeof(config)));
    n=read_fixture(interfaces_path,expected);CHECK(!gateway_application_network_boot(&environment));
    CHECK(read_fixture(interfaces_path,bytes)==n&&!memcmp(bytes,expected,n));
    gateway_application_request_stop(&f.application);step_to_stop(&f);
    CHECK(!gateway_application_init(&f.application,&f.dependencies));step_to_runtime(&f);
    CHECK(!memcmp(&f.application.coordinator.selected,&config,sizeof(config)));
    CHECK(!strcmp(f.application.network.confirmed.settings.lan[1].address,"192.168.3.126"));
    cleanup(&f);
    printf("moxa2 DNS archive lifecycle: %u checks, %u failed\n",checks,failed);
}
int main(void)
{if(getenv("MOXA2_DNS_BASELINE")){moxa2_dns_flow();return failed?1:0;}if(getenv("DHCP_RENEW_ONLY")){renewal_transactions();printf("renewal integration: %u checks, %u failed\n",checks,failed);return failed?1:0;}if(getenv("R7_RETAINED")){use_full=use_service=1;canonical_flow();printf("retained baseline checks=%u failed=%u\n",checks,failed);return failed?1:0;}cold_boot_flow(0);cold_boot_flow(1);renewal_transactions();boot_stage_failures();transaction_supervisor_death();menu_full();interrupted_keep();service_transactions();binding_edits();full_transactions();use_full=1;crash(0);crash(1);use_full=0;printf("application network legacy seqpacket=%u\n",getenv("LAN2_LEGACY_PEER")?1U:0U);invalid_networks();canonical_flow();transactions();faults();bounded_and_bindings();crash(0);crash(1);menu();graceful_stop();printf("application network: %u checks, %u failed\n",checks,failed);return failed?1:0;}
