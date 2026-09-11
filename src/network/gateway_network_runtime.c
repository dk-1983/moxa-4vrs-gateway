#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "network/gateway_network_runtime.h"
#include "network/gateway_network_store.h"
#include "network/gateway_network_system.h"
#include "network/gateway_network_import_policy.h"

static core_tick_t monotonic(void *unused)
{
    struct timespec t;(void)unused;
    if(clock_gettime(CLOCK_MONOTONIC,&t))_exit(125);
    return (core_tick_t)((unsigned long)t.tv_sec*1000UL+(unsigned long)t.tv_nsec/1000000UL);
}
static int read_text(const char *path,char *out,size_t capacity)
{
    struct stat st;int fd;ssize_t n;size_t used=0;
    if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(size_t)st.st_size>=capacity)return -1;
    fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;
    while(used<capacity-1U){n=read(fd,out+used,capacity-1U-used);if(n<0&&errno==EINTR)continue;
        if(n<0){close(fd);return -1;}if(!n)break;used+=(size_t)n;}
    close(fd);out[used]=0;
    return used==strlen(out)&&used==(size_t)st.st_size?0:-1;
}
static int load_profile(const char *directory,gateway_network_profile_t *p)
{
    char data[GATEWAY_NETWORK_SNAPSHOT_MAX];size_t n;
    return gateway_network_store_boot(directory,data,sizeof(data),&n)==GATEWAY_NET_STORE_OK?
        gateway_network_profile_decode(data,n,p):-1;
}
static int save_profile(const char *directory,const char *name,const gateway_network_profile_t *p)
{
    char data[GATEWAY_NETWORK_SNAPSHOT_MAX];size_t n;
    if(gateway_network_profile_encode(p,data,sizeof(data),&n))return -1;
    return gateway_network_store_write(directory,name,data,n)==GATEWAY_NET_STORE_OK?0:-1;
}
/* LAN2 hooks/extra options cannot safely be implemented by address ioctls.
 * Keep them untouched and block Apply instead of invoking a shell. */
static int static_scope(const gateway_network_profile_t *p)
{
    const char *s=p->interfaces;unsigned int owned=0;
    if(p->settings.lan[1].mode!=GATEWAY_LAN_STATIC||p->settings.default_lan==2U||
       p->settings.lan[1].gateway[0])return -1;
    while(*s){char line[513],word[64],name[64];size_t n=strcspn(s,"\n");
        if(n>=sizeof(line))return -1;
        memcpy(line,s,n);line[n]=0;s+=n;if(*s)++s;
        if(sscanf(line," %63s",word)!=1||word[0]=='#')continue;
        if(!strcmp(word,"iface")){if(sscanf(line," iface %63s",name)!=1)return -1;owned=!strcmp(name,"eth1");continue;}
        if(!strcmp(word,"auto")||!strcmp(word,"allow-hotplug")){owned=0;continue;}
        if(owned&&strcmp(word,"address")&&strcmp(word,"netmask")&&strcmp(word,"network")&&strcmp(word,"broadcast"))return -1;
    }
    return 0;
}
static int documents_current(const gateway_network_environment_t *e,const gateway_network_profile_t *p)
{
    gateway_network_settings_t imported;
    char current[GATEWAY_NETWORK_FILE_MAX],rendered[GATEWAY_NETWORK_FILE_MAX];size_t n;
    if(read_text(e->interfaces_path,current,sizeof(current))||read_text(e->resolver_path,rendered,sizeof(rendered)))return -1;
    if(e->read_network){
        gateway_network_profile_t good,pending;char bytes[GATEWAY_NETWORK_SNAPSHOT_MAX];size_t size;
        int have_good=gateway_network_store_read(e->store_directory,"good",bytes,sizeof(bytes),&size)==GATEWAY_NET_STORE_OK&&
            !gateway_network_profile_decode(bytes,size,&good);
        /* The disk documents may still describe the previous confirmed
         * generation, until boot or the next transaction materializes it.
         * Only exact known generations qualify; never normalize foreign drift. */
        if(strcmp(current,p->interfaces)&&(!have_good||strcmp(current,good.interfaces)))return -1;
        if(e->service&&p->settings.automatic_dns){
            if(gateway_network_import(p->interfaces,strlen(p->interfaces),rendered,strlen(rendered),&imported))return -1;
            char expected[GATEWAY_NETWORK_FILE_MAX],actual[GATEWAY_NETWORK_FILE_MAX];
            const char empty[2][16]={{0},{0}};size_t expected_n,actual_n;
            /* On boot no lease survives. Only owned numeric nameservers may
             * differ; restoring clears them, retaining search/options bytes. */
            if(!gateway_network_resolver(p->resolver,strlen(p->resolver),empty,expected,sizeof(expected),&expected_n)&&
               !gateway_network_resolver(rendered,strlen(rendered),empty,actual,sizeof(actual),&actual_n)&&
               expected_n==actual_n&&!memcmp(expected,actual,expected_n))return 0;
            gateway_network_service_status_t status;char resolver[GATEWAY_NETWORK_FILE_MAX];size_t count;
            if(!gateway_network_service_query(e->service,&status)&&status.ready&&!status.error&&
               !gateway_network_resolver(p->resolver,strlen(p->resolver),(const char (*)[16])status.effective.dns,resolver,sizeof(resolver),&count)&&
               !strcmp(rendered,resolver))return 0;
        }
        /* Resolver is a live resource. A crash during Apply can leave the
         * staged resolver installed; recognizing it authorizes only restoration
         * of confirmed, never promotion of candidate at boot. */
        if(strcmp(rendered,p->resolver)&&(!have_good||strcmp(rendered,good.resolver))){
            int match=gateway_network_store_read(e->store_directory,"candidate",bytes,sizeof(bytes),&size)==GATEWAY_NET_STORE_OK&&
               !gateway_network_profile_decode(bytes,size,&pending)&&!strcmp(rendered,pending.resolver);
            if(!match){
                /* A killed durable writer may have renamed confirmed while
                 * commit.guard still authorizes only the old generation. */
                if(gateway_network_store_read(e->store_directory,"commit.guard",bytes,sizeof(bytes),&size)!=GATEWAY_NET_STORE_OK||
                   gateway_network_store_read(e->store_directory,"confirmed",bytes,sizeof(bytes),&size)!=GATEWAY_NET_STORE_OK||
                   gateway_network_profile_decode(bytes,size,&pending)||strcmp(rendered,pending.resolver))return -1;
            }
        }
        return 0;
    }
    /* Full ownership above recognizes exact independently validated store
     * generations: an in-flight resolver may precede interface materialization.
     * A legacy import has no such transaction authority and must agree now. */
    if(gateway_network_import(current,strlen(current),rendered,strlen(rendered),&imported))return -1;
    /* Rendering must not conceal an externally changed LAN1/route/DNS. */
    if(imported.lan[1].mode!=p->settings.lan[1].mode)return -1;
    strcpy(imported.lan[1].address,p->settings.lan[1].address);
    strcpy(imported.lan[1].netmask,p->settings.lan[1].netmask);
    if(memcmp(&imported,&p->settings,sizeof(imported)))return -1;
    if(read_text(e->interfaces_path,current,sizeof(current))||
       gateway_network_render(current,strlen(current),&p->settings,rendered,sizeof(rendered),&n)||
       strcmp(rendered,p->interfaces))return -1;
    if(read_text(e->resolver_path,current,sizeof(current))||strcmp(current,p->resolver))return -1;
    return 0;
}
static int read_real(void *unused,gateway_lan_observation_t *lan)
{
    gateway_network_observation_t o;(void)unused;
    if(gateway_network_observe(&o)||o.unsupported)return -1;
    *lan=o.lan[1];return lan->present?0:-1;
}
static void sockaddr_set(struct sockaddr *out,unsigned long address)
{
    struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(address);
    memcpy(out,&a,sizeof(*out));
}
/* Reject overlap with every other currently addressed interface, including
 * vendor eth2. Fixed ioctl inventory, no commands/hooks and no route/DNS writes. */
static int write_real(void *unused,const gateway_lan_observation_t *lan)
{
    int fd,r=-1;struct ifreq request,inventory[32];struct ifconf list;
    unsigned long ip,mask,broadcast;unsigned int i,count;(void)unused;
    if(gateway_ipv4_parse(lan->address,&ip)||gateway_ipv4_parse(lan->netmask,&mask)||
       gateway_ipv4_parse(lan->broadcast,&broadcast))return -1;
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
    memset(&list,0,sizeof(list));list.ifc_len=sizeof(inventory);list.ifc_req=inventory;
    if(ioctl(fd,SIOCGIFCONF,&list)||list.ifc_len>=(int)sizeof(inventory))goto done;
    count=(unsigned int)list.ifc_len/sizeof(struct ifreq);
    for(i=0;i<count;++i){unsigned long other,other_mask;struct sockaddr_in a;
        if(!strcmp(inventory[i].ifr_name,"eth1")||!strncmp(inventory[i].ifr_name,"lo",3))continue;
        memcpy(&a,&inventory[i].ifr_addr,sizeof(a));other=ntohl(a.sin_addr.s_addr);
        request=inventory[i];if(ioctl(fd,SIOCGIFNETMASK,&request))goto done;
        memcpy(&a,&request.ifr_netmask,sizeof(a));other_mask=ntohl(a.sin_addr.s_addr);
        if((ip&(mask&other_mask))==(other&(mask&other_mask)))goto done;
    }
    memset(&request,0,sizeof(request));strcpy(request.ifr_name,"eth1");
    sockaddr_set(&request.ifr_addr,ip);if(ioctl(fd,SIOCSIFADDR,&request))goto done;
    sockaddr_set(&request.ifr_netmask,mask);if(ioctl(fd,SIOCSIFNETMASK,&request))goto done;
    sockaddr_set(&request.ifr_broadaddr,broadcast);if(ioctl(fd,SIOCSIFBRDADDR,&request))goto done;
    if(ioctl(fd,SIOCGIFFLAGS,&request))goto done;
    if(lan->up)request.ifr_flags|=IFF_UP;else request.ifr_flags&=(short)~IFF_UP;
    if(ioctl(fd,SIOCSIFFLAGS,&request))goto done;
    r=0;
done:close(fd);return r;
}
static int read_network(void *unused,gateway_network_observation_t *out)
{(void)unused;return gateway_network_observe(out)||out->unsupported?-1:0;}
static int write_network(void *unused,const gateway_network_observation_t *desired,const char *resolver)
{(void)unused;return gateway_network_system_write(desired,"/etc/resolv.conf",resolver);}
static int profile_scope(const gateway_network_environment_t *e,const gateway_network_profile_t *p)
{
    const char *s=p->interfaces;int owned=0;
    if(!e->read_network)return static_scope(p);
    if(!e->service&&(p->settings.lan[0].mode!=GATEWAY_LAN_STATIC||p->settings.lan[1].mode!=GATEWAY_LAN_STATIC||p->settings.automatic_dns))return -1;
    while(*s){char line[513],key[64],name[64];size_t n=strcspn(s,"\n");
        if(n>=sizeof(line))return -1;
        memcpy(line,s,n);line[n]=0;s+=n;if(*s)++s;
        if(sscanf(line," %63s",key)!=1||key[0]=='#')continue;
        if(!strcmp(key,"iface")){if(sscanf(line," iface %63s",name)!=1)return -1;
            if(!strncmp(name,"eth0:",5U)||!strncmp(name,"eth1:",5U)||!strncmp(name,"eth0.",5U)||!strncmp(name,"eth1.",5U))return -1;
            owned=!strcmp(name,"eth0")||!strcmp(name,"eth1");continue;}
        if(!strcmp(key,"auto")||!strcmp(key,"allow-hotplug")){owned=0;continue;}
        /* Static vendor DNS metadata must not outlive a switch to lease DNS.
         * New candidates remove it; refuse ambiguous retained auto-DNS profiles. */
        if(owned&&p->settings.automatic_dns&&(!strcmp(key,"dns-nameserver")||!strcmp(key,"dns-nameservers")))return -1;
        if(owned&&strcmp(key,"address")&&strcmp(key,"netmask")&&strcmp(key,"network")&&strcmp(key,"broadcast")&&strcmp(key,"gateway")&&strcmp(key,"dns-nameserver")&&strcmp(key,"dns-nameservers"))return -1;
    }
    return 0;
}
static gateway_network_environment_t production={"/etc/4vrs-network","/etc/network/interfaces",
    "/etc/resolv.conf",read_real,write_real,0,monotonic,0,"/var/run/4vrs-network-supervisor.log",read_network,write_network,0,"/proc","/etc/dhcpc"};
const gateway_network_environment_t *gateway_network_environment_production(void)
{production.service=gateway_network_service_production();return &production;}

int gateway_network_enroll_detailed(const gateway_network_environment_t *e,
                                    const char *const bindings[8],const char **stage)
{
    gateway_network_profile_t p;gateway_lan_observation_t live;gateway_network_observation_t all;char data[GATEWAY_NETWORK_SNAPSHOT_MAX];
    unsigned int i;size_t n;int lock,r=-1;const char *at="arguments";
    if(stage)*stage=at;
    if(!e||!bindings||!e->store_directory||!e->interfaces_path||!e->resolver_path||!e->read_lan2)return -1;
    if(stage)*stage="store-lock";
    lock=gateway_network_store_lock(e->store_directory);if(lock<0)return -1;
    memset(&p,0,sizeof(p));
    at="read-interfaces";if(read_text(e->interfaces_path,p.interfaces,sizeof(p.interfaces)))goto done;
    at="read-resolver";if(read_text(e->resolver_path,p.resolver,sizeof(p.resolver)))goto done;
    at="import";if(gateway_network_import(p.interfaces,strlen(p.interfaces),p.resolver,strlen(p.resolver),&p.settings))goto done;
    at="static-scope";if(profile_scope(e,&p))goto done;
    if(e->read_network){
        at="observe";if(e->read_network(e->context,&all)||all.unsupported)goto done;
        if(p.settings.lan[0].mode==GATEWAY_LAN_DHCP_CLIENT||p.settings.lan[1].mode==GATEWAY_LAN_DHCP_CLIENT){
            at="dhcp-policy-import";
            if(gateway_network_import_policy(e->import_proc_directory,e->import_lease_directory,&p.settings,&all))goto done;
        }
        at="observation-match";
        for(i=0;i<2U;++i)if(!all.lan[i].present||!all.lan[i].up||
            (p.settings.lan[i].mode==GATEWAY_LAN_STATIC&&(strcmp(all.lan[i].address,p.settings.lan[i].address)||
            strcmp(all.lan[i].netmask,p.settings.lan[i].netmask))))goto done;
        at="broadcast-match";
        for(i=0;i<2U;++i)if(p.settings.lan[i].mode==GATEWAY_LAN_STATIC){char expected[16];if(gateway_network_profile_broadcast(&p,i,expected)||strcmp(expected,all.lan[i].broadcast))goto done;}
        at="route-dns-match";
        if(all.default_lan!=p.settings.default_lan||memcmp(all.dns,p.settings.dns,sizeof(all.dns))||
            (all.default_lan&&p.settings.lan[all.default_lan-1U].mode==GATEWAY_LAN_STATIC&&strcmp(all.gateway,p.settings.lan[all.default_lan-1U].gateway)))goto done;
    }
    if(!e->read_network){
    at="observe";if(e->read_lan2(e->context,&live))goto done;
    at="observation-match";if(!live.present||!live.up||strcmp(live.address,p.settings.lan[1].address)||
        strcmp(live.netmask,p.settings.lan[1].netmask))goto done;
    at="broadcast-match";
    {unsigned long ip,mask;char b[16];gateway_ipv4_parse(live.address,&ip);gateway_ipv4_parse(live.netmask,&mask);
        gateway_ipv4_format((ip&mask)|(~mask&0xffffffffUL),b);if(strcmp(b,live.broadcast))goto done;}
    }
    at="bindings";
    for(i=0;i<8U;++i){unsigned long address;
        if(!bindings[i]||strlen(bindings[i])>=16U||gateway_ipv4_parse(bindings[i],&address))goto done;
        strcpy(p.original_bind[i],bindings[i]);
        p.affinity[i]=strcmp(bindings[i],e->read_network?all.lan[1].address:p.settings.lan[1].address)?0U:2U;
        if(e->read_network&&!strcmp(bindings[i],all.lan[0].address))p.affinity[i]=1U;
    }
    at="encode";if(gateway_network_profile_encode(&p,data,sizeof(data),&n))goto done;
    at="persist";if(gateway_network_store_adopt(e->store_directory,data,n)!=GATEWAY_NET_STORE_OK)goto done;
    at="ok";r=0;
 done:if(stage)*stage=at;gateway_network_store_unlock(lock);return r;
}
int gateway_network_enroll(const gateway_network_environment_t *e,const char *const bindings[8])
{return gateway_network_enroll_detailed(e,bindings,0);}

typedef struct worker {
    const gateway_network_environment_t *environment;
    gateway_network_profile_t confirmed,candidate;
    gateway_lan_observation_t desired;
    gateway_network_observation_t original,desired_network;
    const char *desired_resolver;
    int job, restoring, job_result,commit_pending,commit_canceled;
    int trace_fd;unsigned int trace_records;
    gateway_network_manager_t *manager;
    int owner_fd;
    unsigned int owner_request,owner_trace_records,owner_trace_flags;
    gateway_network_service_status_t owner_status;
} worker_t;
static int trace_open(const char *path)
{
    struct stat st;int fd;
    if(!path)return -1;
    fd=open(path,O_WRONLY|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0600);if(fd<0)return -1;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_uid!=geteuid()||
       fcntl(fd,F_SETFD,FD_CLOEXEC)<0||ftruncate(fd,0)){close(fd);return -1;}
    return fd;
}
static void trace_event(void *context,core_tick_t now,const gateway_network_manager_t *m,
    gateway_network_supervisor_event_t event,int detail,unsigned long iterations)
{
    worker_t *w=context;char line[160];int n;
    if(w->trace_fd<0||w->trace_records>=GATEWAY_NETWORK_TRACE_LIMIT)return;
    ++w->trace_records;
    n=snprintf(line,sizeof(line),"ms=%lu state=%u reason=%u error=%d event=%u detail=%d loops=%lu\n",
        (unsigned long)now,(unsigned int)m->state,(unsigned int)m->rollback_reason,
        m->error_code,(unsigned int)event,detail,iterations);
    /* One bounded write; diagnostic failure must not block recovery. No retry,
     * sync, configuration bytes, unbounded history or client-owned descriptors. */
    if(n<0||(size_t)n>=sizeof(line)||write(w->trace_fd,line,(size_t)n)!=n){close(w->trace_fd);w->trace_fd=-1;}
}
static void worker_event(worker_t *w,gateway_network_supervisor_event_t event,int detail)
{
    trace_event(w,(w->environment->clock?w->environment->clock:monotonic)(w->environment->clock_context),
        w->manager,event,detail,0);
}

/* Reserve the majority of the existing 64-record budget for recovery events.
 * Log state changes and accepted lease deadlines, never addresses or packets. */
static void owner_event(worker_t *w,const gateway_network_service_status_t *s)
{
    unsigned int i,flags=(s->ready?1U:0U)|(s->settled?2U:0U)|
        (s->lease_valid[0]?4U:0U)|(s->lease_valid[1]?8U:0U)|
        ((s->dhcp_state[0]&15U)<<4)|((s->dhcp_state[1]&15U)<<8)|((s->error&255U)<<12);
    core_tick_t now=(w->environment->clock?w->environment->clock:monotonic)(w->environment->clock_context);
    if(w->owner_trace_records<16U&&(!w->owner_trace_records||flags!=w->owner_trace_flags)){
        worker_event(w,GATEWAY_NET_EVENT_OWNER_STATUS,(int)flags);++w->owner_trace_records;
    }
    w->owner_trace_flags=flags;
    for(i=0;i<2U;++i)if(w->owner_trace_records<16U&&s->lease_valid[i]&&
        (!w->owner_status.lease_valid[i]||s->lease_expiry[i]!=w->owner_status.lease_expiry[i])){
        int32_t remaining=(int32_t)(s->lease_expiry[i]-now);
        worker_event(w,i?GATEWAY_NET_EVENT_LEASE_LAN2:GATEWAY_NET_EVENT_LEASE_LAN1,
                     remaining>0?(int)((uint32_t)remaining/1000U):0);
        ++w->owner_trace_records;
    }
}

static int launch_write(worker_t *w)
{
    pid_t pid=fork();if(pid<0)return -1;
    if(!pid){if(gateway_network_process_isolate(-1))_exit(120);
        _exit((w->environment->write_network?
            w->environment->write_network(w->environment->context,&w->desired_network,w->desired_resolver):
            w->environment->write_lan2(w->environment->context,&w->desired))?1:0);}
    w->job=(int)pid;w->job_result=0;worker_event(w,GATEWAY_NET_EVENT_WORKER_START,w->job);return 0;
}
static int desired_profile(worker_t *w,const gateway_network_profile_t *p)
{
    unsigned long ip,mask;
    if(w->environment->read_network){unsigned int i;
        w->desired_network=w->original;w->desired_resolver=p->resolver;
        for(i=0;i<2U;++i){
            gateway_lan_observation_t *lan=&w->desired_network.lan[i];
            if(p->settings.lan[i].mode==GATEWAY_LAN_DHCP_CLIENT)continue;
            if(gateway_ipv4_parse(p->settings.lan[i].address,&ip)||gateway_ipv4_parse(p->settings.lan[i].netmask,&mask))return -1;
            strcpy(lan->address,p->settings.lan[i].address);strcpy(lan->netmask,p->settings.lan[i].netmask);
            if(gateway_network_profile_broadcast(p,i,lan->broadcast))return -1;
        }
        w->desired_network.default_lan=p->settings.default_lan;w->desired_network.default_routes=p->settings.default_lan?1U:0U;
        w->desired_network.gateway[0]=0;
        if(p->settings.default_lan){
            if(p->settings.lan[p->settings.default_lan-1U].mode==GATEWAY_LAN_DHCP_CLIENT){
                w->desired_network.default_lan=w->original.default_lan;w->desired_network.default_routes=w->original.default_routes;
                strcpy(w->desired_network.gateway,w->original.gateway);
            }else strcpy(w->desired_network.gateway,p->settings.lan[p->settings.default_lan-1U].gateway);
        }
        if(!p->settings.automatic_dns)memcpy(w->desired_network.dns,p->settings.dns,sizeof(w->desired_network.dns));
        return 0;
    }
    memset(&w->desired,0,sizeof(w->desired));w->desired.present=w->desired.up=1;
    strcpy(w->desired.address,p->settings.lan[1].address);strcpy(w->desired.netmask,p->settings.lan[1].netmask);
    if(gateway_ipv4_parse(w->desired.address,&ip)||gateway_ipv4_parse(w->desired.netmask,&mask))return -1;
    gateway_ipv4_format((ip&mask)|(~mask&0xffffffffUL),w->desired.broadcast);return 0;
}
static int restore(void *context);
static int worker_poll(void *context)
{
    worker_t *w=context;gateway_lan_observation_t live;int status;pid_t pid;
    if(w->commit_canceled){
        pid=waitpid((pid_t)w->job,&status,WNOHANG);
        if(!pid||(pid<0&&errno==EINTR))return 0;
        worker_event(w,GATEWAY_NET_EVENT_WORKER_REAP,pid<0?-errno:status);
        w->job=w->commit_pending=w->commit_canceled=0;
        if(pid>0&&WIFEXITED(status)&&!WEXITSTATUS(status))return 2; /* Durable Keep won the race. */
        if(gateway_network_store_abort_commit(w->environment->store_directory))return -1;
        return restore(w)?-1:0;
    }
    if(w->owner_fd>=0){gateway_network_service_status_t observed;int got=gateway_network_service_read(w->owner_fd,&observed);
        if(got<0)return -1;
        if(got>0&&observed.request==w->owner_request){owner_event(w,&observed);w->owner_status=observed;}
        if(w->owner_status.request!=w->owner_request)return 0;
        return w->owner_status.error?-1:(w->restoring?w->owner_status.settled:w->owner_status.ready)?1:0;
    }
    if(w->job){
        pid=waitpid((pid_t)w->job,&status,WNOHANG);
        if(pid==0||(pid<0&&errno==EINTR))return 0;
        worker_event(w,GATEWAY_NET_EVENT_WORKER_REAP,pid<0?-errno:status);
        w->job=0;
        if(w->restoring){w->restoring=0;return launch_write(w)?-1:0;}
        if(pid<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)w->job_result=-1;
    }
    if(w->job_result)return -1;
    if(w->environment->read_network){gateway_network_observation_t all;unsigned int i;
        if(w->environment->read_network(w->environment->context,&all)||all.unsupported)return -1;
        for(i=0;i<2U;++i)if(!all.lan[i].present||all.lan[i].up!=w->desired_network.lan[i].up||
            strcmp(all.lan[i].address,w->desired_network.lan[i].address)||strcmp(all.lan[i].netmask,w->desired_network.lan[i].netmask)||
            strcmp(all.lan[i].broadcast,w->desired_network.lan[i].broadcast))return -1;
        return all.default_lan==w->desired_network.default_lan&&!strcmp(all.gateway,w->desired_network.gateway)&&
            !memcmp(all.dns,w->desired_network.dns,sizeof(all.dns))?1:-1;
    }
    if(w->environment->read_lan2(w->environment->context,&live))return -1;
    return live.present&&live.up==w->desired.up&&!strcmp(live.address,w->desired.address)&&
        !strcmp(live.netmask,w->desired.netmask)&&!strcmp(live.broadcast,w->desired.broadcast)?1:-1;
}
static int stage(void *context,const gateway_network_settings_t *settings)
{
    worker_t *w=context;gateway_network_settings_t allowed=w->confirmed.settings;
    if(gateway_network_store_abort_commit(w->environment->store_directory))return -17;
    allowed.lan[1]=settings->lan[1];
    if(w->environment->read_network){
        allowed=*settings;
        if(w->environment->read_network(w->environment->context,&w->original)||w->original.unsupported)return -1;
    }
    if(memcmp(&allowed,settings,sizeof(allowed)))return -10;
    if(profile_scope(w->environment,&w->confirmed))return -11;
    if(documents_current(w->environment,&w->confirmed))return -12;
    if(desired_profile(w,&w->confirmed)||worker_poll(w)!=1)return -13;
    if(gateway_network_profile_candidate(&w->confirmed,settings,&w->candidate))return -14;
    if(profile_scope(w->environment,&w->candidate))return -15;
    if(w->environment->read_network&&(gateway_network_file_replace(w->environment->interfaces_path,w->confirmed.interfaces)||
        (!w->environment->service&&gateway_network_file_replace(w->environment->resolver_path,w->confirmed.resolver))))return -16;
    return save_profile(w->environment->store_directory,"candidate",&w->candidate);
}
static int apply(void *context)
{
    worker_t *w=context;
    if(w->environment->service){
        w->owner_fd=gateway_network_service_connect(w->environment->service,1);
        if(w->owner_fd<0)return -1;
        ++w->owner_request;return gateway_network_service_command(w->owner_fd,'A',w->owner_request);
    }
    if(desired_profile(w,&w->candidate))return -1;
    return launch_write(w);
}
static int restore(void *context)
{
    worker_t *w=context;
    if(w->commit_pending){
        if(kill((pid_t)w->job,SIGKILL)&&errno!=ESRCH)return -1;
        w->commit_canceled=1;return 0;
    }
    if(gateway_network_store_abort_commit(w->environment->store_directory))return -1;
    if(w->environment->service){
        if(w->owner_fd<0)w->owner_fd=gateway_network_service_connect(w->environment->service,1);
        if(w->owner_fd<0)return -1;
        w->restoring=1;++w->owner_request;return gateway_network_service_command(w->owner_fd,'R',w->owner_request);
    }
    if(load_profile(w->environment->store_directory,&w->confirmed)||desired_profile(w,&w->confirmed))return -1;
    if(w->job){int result=kill((pid_t)w->job,SIGKILL),error=result<0?errno:0;
        worker_event(w,GATEWAY_NET_EVENT_WORKER_KILL,-error);
        if(result<0&&error!=ESRCH)return -1;
        w->restoring=1;return 0;}
    return launch_write(w);
}
static int commit(void *context)
{
    worker_t *w=context;int status;pid_t pid;
    if(!w->commit_pending){
        pid=fork();if(pid<0)return -1;
        if(!pid){gateway_network_store_result_t result;
            if(gateway_network_process_isolate(-1))_exit(120);
            result=gateway_network_store_confirm(w->environment->store_directory);
            _exit(result==GATEWAY_NET_STORE_OK?0:result==GATEWAY_NET_STORE_UNCERTAIN?2:1);
        }
        w->job=(int)pid;w->commit_pending=1;worker_event(w,GATEWAY_NET_EVENT_WORKER_START,w->job);return 1;
    }
    pid=waitpid((pid_t)w->job,&status,WNOHANG);if(!pid||(pid<0&&errno==EINTR))return 1;
    worker_event(w,GATEWAY_NET_EVENT_WORKER_REAP,pid<0?-errno:status);
    w->job=w->commit_pending=0;
    if(pid>0&&WIFEXITED(status)&&!WEXITSTATUS(status))return 0;
    if(pid>0&&WIFEXITED(status)&&WEXITSTATUS(status)==2){gateway_network_profile_t baseline;
        if(gateway_network_store_abort_commit(w->environment->store_directory)||load_profile(w->environment->store_directory,&baseline)||
           memcmp(&baseline,&w->confirmed,sizeof(baseline)))return -2;
    }
    return -1;
}
static int discard(void *context)
{worker_t *w=context;return gateway_network_store_discard(w->environment->store_directory)==GATEWAY_NET_STORE_OK?0:-1;}
static const gateway_network_manager_ops_t operations={stage,apply,worker_poll,commit,restore,discard};

int gateway_network_process_isolate(int keep)
{
    DIR *d;struct dirent *entry;int directory_fd,fd;
    if(setsid()<0)return -1;
    d=opendir("/proc/self/fd");if(!d)return -1;directory_fd=dirfd(d);
    for(;;){char *end;long number;errno=0;entry=readdir(d);if(!entry){if(errno){closedir(d);return -1;}break;}
        number=strtol(entry->d_name,&end,10);
        if(!*end&&number>=0&&number!=keep&&number!=directory_fd)close((int)number);}
    closedir(d);fd=open("/dev/null",O_RDWR);if(fd<0)return -1;
    if(fd!=0&&dup2(fd,0)<0)return -1;
    if(dup2(fd,1)<0||dup2(fd,2)<0)return -1;
    if(fd>2&&fd!=keep)close(fd);
    return 0;
}
int gateway_network_runtime_init(gateway_network_runtime_t *r,const gateway_network_environment_t *e)
{
    int lock;if(!r)return -1;
    memset(r,0,sizeof(*r));r->fd=r->observer_fd=-1;r->environment=e;
    if(!e)return 0;
    if(!e->store_directory||!e->interfaces_path||!e->resolver_path||!e->read_lan2||!e->write_lan2||
       (!!e->read_network != !!e->write_network))return -1;
    lock=gateway_network_store_lock(e->store_directory);if(lock<0)return -1;
    r->available=!load_profile(e->store_directory,&r->confirmed)&&!profile_scope(e,&r->confirmed);
    gateway_network_store_unlock(lock);
    r->status.state=r->available?GATEWAY_NETWORK_IDLE:GATEWAY_NETWORK_UNAVAILABLE;
    r->observe_at=(e->clock?e->clock:monotonic)(e->clock_context);
    return r->available?0:-1;
}
int gateway_network_runtime_busy(const gateway_network_runtime_t *r){return r&&(r->pid>0||r->recovering||(r->stopping&&r->observer_pid>0));}
int gateway_network_runtime_start(gateway_network_runtime_t *r,const gateway_network_settings_t *candidate)
{
    int pair[2],flags,i;pid_t pid;
    if(!r||!r->available||r->pid||r->recovering||r->stopping||r->status.state==GATEWAY_NETWORK_ROLLBACK_FAILED||
       r->status.state==GATEWAY_NETWORK_DURABILITY_UNCERTAIN||
       gateway_network_profile_candidate(&r->confirmed,candidate,&r->candidate))return -1;
    if(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair))return -1;
    for(i=0;i<2;++i)if(pair[i]<3){
        int moved=fcntl(pair[i],F_DUPFD,3);
        if(moved<0){close(pair[0]);close(pair[1]);return -1;}
        close(pair[i]);pair[i]=moved;
    }
    flags=fcntl(pair[0],F_GETFL,0);
    if(flags<0||fcntl(pair[0],F_SETFL,flags|O_NONBLOCK)<0||fcntl(pair[0],F_SETFD,FD_CLOEXEC)<0){close(pair[0]);close(pair[1]);return -1;}
    pid=fork();if(pid<0){close(pair[0]);close(pair[1]);return -1;}
    if(!pid){worker_t w;gateway_network_manager_t manager;int lock,result;
        close(pair[0]);if(gateway_network_process_isolate(pair[1]))_exit(120);
        lock=gateway_network_store_lock(r->environment->store_directory);if(lock<0)_exit(121);
        memset(&w,0,sizeof(w));w.environment=r->environment;w.trace_fd=w.owner_fd=-1;w.manager=&manager;
        if(load_profile(w.environment->store_directory,&w.confirmed)||
           memcmp(&w.confirmed,&r->confirmed,sizeof(w.confirmed)))_exit(122);
        gateway_network_manager_init(&manager,&operations,&w);
        w.trace_fd=trace_open(w.environment->trace_path);
        result=gateway_network_supervisor_run_traced(pair[1],&manager,candidate,
            w.environment->clock?w.environment->clock:monotonic,w.environment->clock_context,trace_event,&w);
        if(w.job){unsigned int attempt;int status;pid_t reaped;
            int killed=kill((pid_t)w.job,SIGKILL),error=killed<0?errno:0;
            worker_event(&w,GATEWAY_NET_EVENT_WORKER_KILL,-error);
            /* A failed rollback must still reap a killable mutator. Never wait
             * indefinitely for uninterruptible kernel work: leave failure explicit. */
            for(attempt=0;attempt<100U;++attempt){
                reaped=waitpid((pid_t)w.job,&status,WNOHANG);
                if(reaped>0||(reaped<0&&errno!=EINTR))break;
                usleep(10000);
            }
            worker_event(&w,GATEWAY_NET_EVENT_WORKER_REAP,attempt==100U?-ETIMEDOUT:reaped<0?-errno:status);
        }
        if(w.trace_fd>=0)close(w.trace_fd);
        gateway_network_store_unlock(lock);close(pair[1]);_exit(result?1:0);
    }
    close(pair[1]);r->fd=pair[0];r->pid=(int)pid;r->status.state=GATEWAY_NETWORK_APPLYING;
    r->status.deadline=0;r->status.rollback_reason=GATEWAY_NETWORK_REASON_NONE;r->status.error_code=0;
    r->binding_index=r->binding_wait=r->binding_ack=r->stop_sent=0;return 0;
}
int gateway_network_runtime_command(gateway_network_runtime_t *r,char command)
{return r&&r->fd>=0&&send(r->fd,&command,1,MSG_NOSIGNAL)==1?0:-1;}
void gateway_network_runtime_disconnect(gateway_network_runtime_t *r)
{if(r&&r->fd>=0){close(r->fd);r->fd=-1;}}
void gateway_network_runtime_stop(gateway_network_runtime_t *r)
{
    if(!r)return;
    r->stopping=1;r->recovering=0;
    if(r->observer_pid>0)(void)kill((pid_t)r->observer_pid,SIGKILL);
    if(!r||!r->pid||r->stop_sent)return;
    if(gateway_network_runtime_command(r,'D'))gateway_network_runtime_disconnect(r);
    r->stop_sent=1;
}
static void accept_observation(gateway_network_runtime_t *r,const gateway_network_service_status_t *snapshot)
{
 r->observed=snapshot->observed;r->observed_valid=1;r->observation_error=(snapshot->error||snapshot->observed.unsupported)?1U:0U;
 memcpy(r->lease_valid,snapshot->lease_valid,sizeof(r->lease_valid));memcpy(r->dhcp_state,snapshot->dhcp_state,sizeof(r->dhcp_state));
 memcpy(r->lease_expiry,snapshot->lease_expiry,sizeof(r->lease_expiry));
 if(r->recovering){
  const gateway_network_profile_t *p=snapshot->recovery_generation==2U?&r->candidate:&r->confirmed;
  r->observed_valid=snapshot->recovery_generation&&snapshot->settled&&!r->observation_error&&!memcmp(&snapshot->policy,&p->settings,sizeof(p->settings));
  if(r->observed_valid&&snapshot->recovery_generation==2U){r->confirmed=r->candidate;r->recovery_kept=1;}
 }
}
static void read_observer(gateway_network_runtime_t *r)
{
    gateway_network_service_status_t snapshot;ssize_t n;
    if(r->observer_received||r->observer_failed)return;
    n=read(r->observer_fd,&snapshot,sizeof(snapshot));
    if(n==(ssize_t)sizeof(snapshot)){
        /* A complete frame may precede child exit by several application ticks.
         * Remember consumption even when its epoch has become obsolete. */
        r->observer_received=1;
        if(r->observer_epoch==r->observation_epoch)accept_observation(r,&snapshot);
    }else if(n>=0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)){
        /* Child writes one fixed-size frame. Partial/EOF is never a snapshot. */
        r->observer_failed=1;
    }
}
static void poll_observer(gateway_network_runtime_t *r)
{
    core_tick_t now;int status,pair[2],flags;pid_t pid;
    gateway_network_service_status_t snapshot;
    const gateway_network_environment_t *e=r->environment;
    if(!e||!e->read_network)return;
    now=(e->clock?e->clock:monotonic)(e->clock_context);
    if(r->observer_pid){
        read_observer(r);
        pid=waitpid((pid_t)r->observer_pid,&status,WNOHANG);
        if(pid==r->observer_pid||(pid<0&&errno!=EINTR)){
            if(pid>0&&WIFEXITED(status)&&!WEXITSTATUS(status))read_observer(r);
            if(pid<0||!WIFEXITED(status)||WEXITSTATUS(status)||!r->observer_received)r->observer_failed=1;
            if(r->observer_failed&&r->observer_epoch==r->observation_epoch)r->observation_error=1;
            close(r->observer_fd);r->observer_fd=-1;r->observer_pid=0;r->observe_at=now+(r->pid?250U:1000U);
            if(r->observer_epoch!=r->observation_epoch)r->observe_at=now;
            if(r->observation_error)memset(r->lease_valid,0,sizeof(r->lease_valid));
        }else if((int32_t)(now-r->observer_deadline)>=0){
            (void)kill((pid_t)r->observer_pid,SIGKILL);r->observer_failed=1;
            if(r->observer_epoch==r->observation_epoch){r->observation_error=1;memset(r->lease_valid,0,sizeof(r->lease_valid));}
        }
        return;
    }
    if(r->stopping||!r->available||(int32_t)(now-r->observe_at)<0)return;
    if(pipe(pair)){r->observation_error=1;r->observe_at=now+1000U;return;}
    {unsigned int i;for(i=0;i<2U;++i)if(pair[i]<3){int moved=fcntl(pair[i],F_DUPFD,3);
        if(moved<0){close(pair[0]);close(pair[1]);r->observation_error=1;return;}
        close(pair[i]);pair[i]=moved;
    }}
    flags=fcntl(pair[0],F_GETFL,0);
    if(flags<0||fcntl(pair[0],F_SETFL,flags|O_NONBLOCK)<0||fcntl(pair[0],F_SETFD,FD_CLOEXEC)<0){close(pair[0]);close(pair[1]);return;}
    pid=fork();if(pid<0){close(pair[0]);close(pair[1]);r->observation_error=1;r->observe_at=now+1000U;return;}
    if(!pid){close(pair[0]);if(gateway_network_process_isolate(pair[1]))_exit(1);
        memset(&snapshot,0,sizeof(snapshot));
        if(!e->service||gateway_network_service_query(e->service,&snapshot)){
            if(e->read_network(e->context,&snapshot.observed))_exit(1);
        }
        if(r->recovering){gateway_network_profile_t stored;
            if(load_profile(e->store_directory,&stored))_exit(1);
            if(!memcmp(&stored,&r->confirmed,sizeof(stored)))snapshot.recovery_generation=1;
            else if(!memcmp(&stored,&r->candidate,sizeof(stored)))snapshot.recovery_generation=2;
            else _exit(1);
        }
        _exit(write(pair[1],&snapshot,sizeof(snapshot))==(ssize_t)sizeof(snapshot)?0:1);}
    close(pair[1]);r->observer_fd=pair[0];r->observer_pid=(int)pid;r->observer_deadline=now+200U;r->observer_epoch=r->observation_epoch;
    r->observer_received=r->observer_failed=0;
}
void gateway_network_runtime_poll(gateway_network_runtime_t *r)
{
    gateway_network_supervisor_status_t s;ssize_t n;int status;pid_t p;
    if(!r)return;
    poll_observer(r);
    if(r->recovering){core_tick_t now=(r->environment->clock?r->environment->clock:monotonic)(r->environment->clock_context);
        if((int32_t)(now-r->recovery_deadline)>=0){r->recovering=0;r->status.state=GATEWAY_NETWORK_ROLLBACK_FAILED;}
    }
    if(!r->pid)return;
    n=r->fd>=0?recv(r->fd,&s,sizeof(s),MSG_DONTWAIT):-1;
    if(n==(ssize_t)sizeof(s)){
        if(s.state!=r->status.state&&(s.state==GATEWAY_NETWORK_WAIT_BINDINGS||s.state==GATEWAY_NETWORK_ROLLBACK_BINDINGS)){
            r->binding_index=r->binding_wait=r->binding_ack=0;
            if(r->environment&&r->environment->service){r->observed_valid=0;++r->observation_epoch;
                r->observe_at=(r->environment->clock?r->environment->clock:monotonic)(r->environment->clock_context);}
        }
        r->status=s;
        if(s.state==GATEWAY_NETWORK_KEPT)r->confirmed=r->candidate;
    }
    /* Drain the terminal frame before reaping; a child can send several states
     * then exit between two application ticks. Work stays one frame per tick. */
    if(n>0)return;
    p=waitpid((pid_t)r->pid,&status,WNOHANG);
    if(p==r->pid){r->pid=0;gateway_network_runtime_disconnect(r);
        if(r->status.state!=GATEWAY_NETWORK_KEPT&&r->status.state!=GATEWAY_NETWORK_REVERTED&&
           r->status.state!=GATEWAY_NETWORK_FAILED&&
           r->status.state!=GATEWAY_NETWORK_ROLLBACK_FAILED&&r->status.state!=GATEWAY_NETWORK_DURABILITY_UNCERTAIN)
        {
            if(r->environment->service&&!r->stopping){
                core_tick_t now=(r->environment->clock?r->environment->clock:monotonic)(r->environment->clock_context);
                r->recovering=1;r->recovery_kept=0;r->recovery_deadline=now+GATEWAY_NETWORK_ROLLBACK_MS;
                r->status.state=GATEWAY_NETWORK_ROLLBACK_BINDINGS;r->binding_index=r->binding_wait=r->binding_ack=0;
                r->observed_valid=0;++r->observation_epoch;r->observe_at=now;
            }else r->status.state=GATEWAY_NETWORK_ROLLBACK_FAILED;
        }
    }
}

int gateway_network_boot_restore_detailed(const gateway_network_environment_t *e,const char **stage)
{
    gateway_network_profile_t p;char current[GATEWAY_NETWORK_FILE_MAX],out[GATEWAY_NETWORK_FILE_MAX];
    char temporary[256],directory[256],*slash;size_t n,used=0;int lock,fd=-1,dfd=-1,result=-1;ssize_t wrote;
    struct stat st;
    const char *at="arguments";
    if(stage)*stage=at;
    if(!e||!e->store_directory||!e->interfaces_path||!e->resolver_path)return -1;
    at="store-lock";if(stage)*stage=at;
    lock=gateway_network_store_lock(e->store_directory);if(lock<0)return -1;
    /* Validate current documents before materializing a known generation.
     * Preserve unowned interfaces/hooks and reject unrecognized drift. */
    at="load-profile";if(load_profile(e->store_directory,&p))goto done;
    at="profile-scope";if(profile_scope(e,&p))goto done;
    at="documents-current";if(documents_current(e,&p))goto done;
    at="read-interfaces";if(read_text(e->interfaces_path,current,sizeof(current)))goto done;
    at="render";if(gateway_network_render(current,strlen(current),&p.settings,out,sizeof(out),&n))goto done;
    if(e->read_network){
        gateway_network_service_status_t status;
        int running=e->service&&!gateway_network_service_query(e->service,&status);
        const char *resolver=p.resolver;char without_lease[GATEWAY_NETWORK_FILE_MAX];size_t count;
        at="render-resolver";
        if(p.settings.automatic_dns){const char empty[2][16]={{0},{0}};
            if(gateway_network_resolver(p.resolver,strlen(p.resolver),empty,without_lease,sizeof(without_lease),&count))goto done;
            resolver=without_lease;
        }
        at="write-interfaces";if(gateway_network_file_replace(e->interfaces_path,p.interfaces))goto done;
        at="write-resolver";if(!running&&gateway_network_file_replace(e->resolver_path,resolver))goto done;
        result=0;
        goto done;
    }
    at="write-interfaces";
    if(strlen(e->interfaces_path)+12U>=sizeof(temporary)||lstat(e->interfaces_path,&st))goto done;
    strcpy(directory,e->interfaces_path);slash=strrchr(directory,'/');if(!slash)goto done;*slash=0;
    dfd=open(directory,O_RDONLY|O_DIRECTORY);if(dfd<0)goto done;
    if(!strcmp(current,out)){result=fsync(dfd)?-1:0;goto done;}
    snprintf(temporary,sizeof(temporary),"%s.4vrs.tmp",e->interfaces_path);
    /* A power loss before rename can leave only this reserved temporary file.
     * Refuse links or foreign owners; never delete the current vendor file. */
    {struct stat temporary_stat;
        if(!lstat(temporary,&temporary_stat)){
            if(!S_ISREG(temporary_stat.st_mode)||temporary_stat.st_uid!=geteuid()||unlink(temporary))goto done;
        }else if(errno!=ENOENT)goto done;
    }
    fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)goto done;
    while(used<n){wrote=write(fd,out+used,n-used);if(wrote<0&&errno==EINTR)continue;if(wrote<=0)goto done;used+=(size_t)wrote;}
    if(fchmod(fd,st.st_mode&0777)||fchown(fd,st.st_uid,st.st_gid)||fsync(fd))goto done;
    if(close(fd)){fd=-1;goto done;}fd=-1;
    if(rename(temporary,e->interfaces_path)||fsync(dfd))goto done;
    result=0;
done:if(stage)*stage=result?at:"ok";if(fd>=0)close(fd);if(dfd>=0)close(dfd);gateway_network_store_unlock(lock);return result;
}

int gateway_network_boot_restore(const gateway_network_environment_t *e)
{return gateway_network_boot_restore_detailed(e,0);}
