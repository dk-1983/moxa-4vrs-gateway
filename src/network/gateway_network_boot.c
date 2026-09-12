#define _GNU_SOURCE
#include "core/platform.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "network/gateway_network_boot.h"
#include "network/gateway_network_runtime.h"
#include "network/gateway_network_store.h"
int gateway_network_unowned_checked(const char *text,size_t length,unsigned int up,const gateway_network_boot_ops_t *ops,void *context)
{
    size_t offset=0;char names[32][16];int present[32],result=0;unsigned int count=0,i,optional_stanzas=0,optional_static=0;
    if(!text||!ops||!ops->action||up>1U||length>GATEWAY_NETWORK_FILE_MAX||memchr(text,0,length))return -1;
    while(offset<length){char line[513],*word,*save;size_t n=0;unsigned int auto_line,iface_line;
        while(offset+n<length&&text[offset+n]!='\n')++n;
        if(n>=sizeof(line))return -1;
        memcpy(line,text+offset,n);line[n]=0;offset+=n;if(offset<length)++offset;
        word=strtok_r(line," \t\r",&save);if(!word||word[0]=='#')continue;
        if(!strcmp(word,"source")||!strcmp(word,"source-directory")||!strcmp(word,"mapping"))return -1;
        auto_line=!strcmp(word,"auto");iface_line=!strcmp(word,"iface");
        if(iface_line){char name[32],family[32],method[32],extra[2];
            int fields=sscanf(save,"%31s %31s %31s %1s",name,family,method,extra);
            if(fields<1)return -1;
            if(!strcmp(name,FOURVRS_OPTIONAL_VENDOR_INTERFACE)){
                ++optional_stanzas;optional_static=fields==3&&!strcmp(family,"inet")&&!strcmp(method,"static");
            }
        }
        if(up?!auto_line:!iface_line)continue;
        while((word=strtok_r(0," \t\r",&save))!=0){size_t j;
            if(word[0]=='#')break;
            if(strlen(word)>=16U||!isalnum((unsigned char)word[0]))return -1;
            for(j=0;word[j];++j)if(!isalnum((unsigned char)word[j])&&word[j]!='_'&&word[j]!='.'&&word[j]!=':'&&word[j]!='-')return -1;
            if(strcmp(word,"" FOURVRS_LAN_PREFIX "0")&&strcmp(word,"" FOURVRS_LAN_PREFIX "1")){
                for(i=0;i<count&&strcmp(names[i],word);++i){}
                if(i==count){if(count==32U)return -1;strcpy(names[count++],word);}
            }
            if(iface_line)break;
        }
    }
    /* Validate the entire inventory before executing its first action. */
    for(i=0;i<count;++i){
        present[i]=ops->present?ops->present(context,names[i]):1;
        if(present[i]<0||present[i]>1){if(ops->diagnostic)ops->diagnostic(context,names[i],"inventory",present[i]);return -1;}
    }
    for(i=0;i<count;++i){int r;
        if(!present[i]){
            /* Only the platform-specific vendor Wi-Fi interface is known optional.
             * Do not skip virtual interface creators or a missing loopback. */
            r=!strcmp(names[i],FOURVRS_OPTIONAL_VENDOR_INTERFACE)&&optional_stanzas==1U&&optional_static?0:-1;
            if(ops->diagnostic)ops->diagnostic(context,names[i],r?"missing-required":"deferred-absent",r);
        }else{
            r=ops->loopback&&!strcmp(names[i],"lo")?ops->loopback(context,text,length,up):ops->action(context,names[i],up);
            if(ops->diagnostic)ops->diagnostic(context,names[i],up?"ifup":"ifdown",r);
        }
        if(r)result=-1;
    }
    return result;
}
int gateway_network_unowned(const char *text,size_t length,unsigned int up,gateway_network_unowned_action_fn action,void *context)
{gateway_network_boot_ops_t ops={0,action,0,0};return gateway_network_unowned_checked(text,length,up,&ops,context);}
int gateway_network_vendor_present(void *unused,const char *name)
{
    struct ifreq request;int fd,r,saved;(void)unused;
    if(!name||strlen(name)>=IFNAMSIZ)return -1;
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
    memset(&request,0,sizeof(request));strcpy(request.ifr_name,name);
    r=ioctl(fd,SIOCGIFINDEX,&request);saved=errno;close(fd);
    if(!r)return request.ifr_ifindex>0?1:-1;
    return saved==ENODEV||saved==ENXIO?0:-1;
}
static int vendor_execute(const char *program,const char *name,unsigned int waits,unsigned int force)
{
    pid_t child,got;int status;unsigned int i;
    if(!program||!name||!waits||waits>2000U)return -1;
    child=fork();if(child<0)return -1;
    if(!child){char *args[4];if(gateway_network_process_isolate(-1))_exit(125);
        args[0]=(char *)program;args[1]=force?FOURVRS_IFDOWN_FORCE:(char *)name;
        args[2]=force?(char *)name:0;args[3]=0;execv(program,args);_exit(126);}
    for(i=0;i<waits;++i){got=waitpid(child,&status,WNOHANG);
        if(got==child)return WIFEXITED(status)?WEXITSTATUS(status):WIFSIGNALED(status)?128+WTERMSIG(status):-1;
        if(got<0&&errno!=EINTR)return -1;
        usleep(5000);}
    (void)kill(-child,SIGKILL);(void)kill(child,SIGKILL);
    for(i=0;i<200U;++i){got=waitpid(child,&status,WNOHANG);
        if(got==child||(got<0&&errno==ECHILD))return -2;
        if(got<0&&errno!=EINTR)return -3;
        usleep(5000);}
    return -3; /* Timeout with unresolved reap; never report successful cleanup. */
}
int gateway_network_vendor_execute(const char *program,const char *name,unsigned int waits)
{return vendor_execute(program,name,waits,0);}
int gateway_network_vendor_execute_forced(const char *program,const char *name,unsigned int waits)
{return vendor_execute(program,name,waits,1);}
static int vendor_action(void *unused,const char *name,unsigned int up)
{(void)unused;return gateway_network_vendor_execute(up?"/sbin/ifup":"/sbin/ifdown",name,2000U);}
static void vendor_diagnostic(void *unused,const char *name,const char *stage,int result)
{(void)unused;fprintf(stderr,"network-vendor iface=%s stage=%s result=%d\n",name,stage,result);}

static void lo_observed(const gateway_network_loopback_ops_t *ops,void *c,int state)
{
    const char *stage=state<0?"lo-observation-error":state==0?"lo-no-address-down":
        state==1?"lo-ready":state==3?"lo-no-address-up":state==4?"lo-address-retained-down":"lo-address-mask-state";
    if(ops->diagnostic)ops->diagnostic(c,"lo",stage,state);
}
int gateway_network_loopback(const char *text,size_t length,unsigned int up,const gateway_network_loopback_ops_t *ops,void *c)
{
    int state,r;const char *stage="lo-observe";
    if(!text||!ops||!ops->observe||!ops->receipt||!ops->action||up>1U)return -1;
    state=ops->observe(c);lo_observed(ops,c,state);if(state<0){r=-1;goto done;}
    if(up&&state==1){
        r=ops->receipt(c,0,text,length);
        stage=r==1?"lo-already-started":r<0?"lo-receipt-error":"lo-restart-required";
        r=r==1?0:-1;goto done;
    }
    /* Vendor down may remove IPv4 while retaining IFF_UP. Both addressless
     * states permit normal vendor up; neither is READY or earns a receipt. */
    if(up&&state!=0&&state!=3&&state!=4){stage="lo-restart-required";r=-1;goto done;}
    stage="lo-receipt-clear";r=ops->receipt(c,1,text,length);if(r)goto done;
    stage=up?"lo-vendor-up":"lo-vendor-forced-down";r=ops->action(c,up);
    if(ops->diagnostic)ops->diagnostic(c,"lo",stage,r);
    if(r)goto done;
    stage="lo-post-observe";state=ops->observe(c);
    lo_observed(ops,c,state);
    if(up?state!=1:(state!=0&&state!=3&&state!=4)){r=-1;goto done;}
    if(up){stage="lo-receipt-save";r=ops->receipt(c,2,text,length);}
done:
    if(ops->diagnostic)ops->diagnostic(c,"lo",stage,r);
    return r;
}

int gateway_network_loopback_observe(void *unused)
{
    struct ifreq q;int fd,r=-1,flags;unsigned long address,mask;(void)unused;
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
    memset(&q,0,sizeof(q));strcpy(q.ifr_name,"lo");
    if(ioctl(fd,SIOCGIFFLAGS,&q))goto done;
    flags=q.ifr_flags;if(!(flags&IFF_LOOPBACK))goto done;
    if(ioctl(fd,SIOCGIFADDR,&q)){
        if(errno==EADDRNOTAVAIL)r=(flags&IFF_UP)?3:0;
        goto done;
    }
    if(q.ifr_addr.sa_family!=AF_INET)goto done;
    address=ntohl(((struct sockaddr_in *)&q.ifr_addr)->sin_addr.s_addr);
    if(!address){r=(flags&IFF_UP)?3:0;goto done;}
    if(ioctl(fd,SIOCGIFNETMASK,&q)||q.ifr_netmask.sa_family!=AF_INET)goto done;
    mask=ntohl(((struct sockaddr_in *)&q.ifr_netmask)->sin_addr.s_addr);
    r=address==0x7f000001UL&&mask==0xff000000UL?((flags&IFF_UP)?1:4):2;
done:
    close(fd);return r;
}

int gateway_network_loopback_receipt(void *context,unsigned int op,const char *text,size_t length)
{
    int fd=*(int *)context;char bytes[GATEWAY_NETWORK_FILE_MAX+1];ssize_t n;
    if(!text||length>GATEWAY_NETWORK_FILE_MAX||op>2U)return -1;
    if(lseek(fd,0,SEEK_SET)<0)return -1;
    if(!op){n=read(fd,bytes,sizeof(bytes));return n<0?-1:(size_t)n==length&&!memcmp(bytes,text,length);}
    if(ftruncate(fd,0))return -1;
    if(op==1)return 0;
    n=write(fd,text,length);
    if(n!=(ssize_t)length||fsync(fd)){(void)ftruncate(fd,0);return -1;}
    return 0;
}
static int lo_action(void *c,unsigned int up)
{(void)c;return up?gateway_network_vendor_execute("/sbin/ifup","lo",2000U):gateway_network_vendor_execute_forced("/sbin/ifdown","lo",2000U);}
static int vendor_loopback(void *unused,const char *text,size_t length,unsigned int up)
{
    int fd,r;struct stat st;struct flock lock;
    const gateway_network_loopback_ops_t ops={gateway_network_loopback_observe,gateway_network_loopback_receipt,lo_action,vendor_diagnostic};
    (void)unused;
    /* Runtime proof of successful hooks, not a replacement for BusyBox ifstate.
     * /var is RAM on this target: receipt cannot survive a cold boot. */
    fd=open("/var/run/4vrs-vendor-lo.receipt",O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(fd<0){vendor_diagnostic(0,"lo","lo-receipt-open",-1);return -1;}
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||st.st_nlink!=1||
       (st.st_mode&077)!=0||fcntl(fd,F_SETLK,&lock)){vendor_diagnostic(0,"lo","lo-receipt-lock",-1);close(fd);return -1;}
    r=gateway_network_loopback(text,length,up,&ops,&fd);close(fd);return r;
}
int gateway_network_vendor_boot_with_ops(const char *store,unsigned int up,const gateway_network_boot_ops_t *ops,void *context)
{
    char bytes[GATEWAY_NETWORK_SNAPSHOT_MAX];size_t n;gateway_network_profile_t p;int r;
    if(!ops)return -1;
    if(gateway_network_store_boot(store,bytes,sizeof(bytes),&n)!=GATEWAY_NET_STORE_OK||gateway_network_profile_decode(bytes,n,&p)){
        if(ops->diagnostic)ops->diagnostic(context,"-","profile",-1);
        return -1;
    }
    r=gateway_network_unowned_checked(p.interfaces,strlen(p.interfaces),up,ops,context);
    if(ops->diagnostic)ops->diagnostic(context,"-","inventory-actions",r);
    return r;
}
int gateway_network_vendor_boot(const char *store,unsigned int up)
{
    const gateway_network_boot_ops_t ops={gateway_network_vendor_present,vendor_action,vendor_diagnostic,vendor_loopback};
    return gateway_network_vendor_boot_with_ops(store,up,&ops,0);
}

int gateway_network_boot_service_with_environment(const gateway_network_service_environment_t *e)
{
    gateway_network_service_status_t status;
    unsigned int i;int fd=gateway_network_service_connect(e,1),result=-1;
    if(fd<0)return -1;
    if(gateway_network_service_command(fd,'R',1)){close(fd);return -1;}
    /* Ack policy acceptance, not DHCP acquisition. Other boot services must
     * not wait for a cable or server; lease readiness remains observable. */
    for(i=0;i<200U;++i){int got=gateway_network_service_read(fd,&status);
        if(got<0)break;
        if(got>0&&status.request==1U){result=status.error?-1:0;break;}usleep(10000);}
    close(fd);return result;
}

int gateway_network_boot_service(void)
{return gateway_network_boot_service_with_environment(gateway_network_service_production());}
