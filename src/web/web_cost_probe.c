/* Standalone bounded diagnostic: disposable inputs, no credentials, no network,
 * no entropy crediting or seed persistence. Not installed as a service. */
#define main certificate_probe_main
#include "web/tls_probe.c"
#undef main
#include <sys/resource.h>
#include "web/web_security.h"
static volatile sig_atomic_t phase;
static void expired(int sig)
{
    const char k[]="stage=kdf reason=bound_expired exit=124\n";
    const char c[]="stage=certificate reason=bound_expired exit=124\n";
    (void)sig;
    if(phase==1) { if(write(2,k,sizeof(k)-1)<0){} }
    else { if(write(2,c,sizeof(c)-1)<0){} }
    _exit(124);
}
static void usage_sample(const char *stage)
{
    struct rusage u; int r, saved;
    memset(&u,0,sizeof(u)); r=getrusage(RUSAGE_SELF,&u); saved=r?errno:0;
    printf("stage=%s getrusage_result=%d errno=%d cpu_user_s=%ld.%06ld cpu_system_s=%ld.%06ld maxrss_native=%ld maxrss_units=unverified maxrss_linux_2_6_10=not_maintained\n",stage,r,saved,(long)u.ru_utime.tv_sec,(long)u.ru_utime.tv_usec,(long)u.ru_stime.tv_sec,(long)u.ru_stime.tv_usec,u.ru_maxrss);
}
static void entropy_count(const char *stage)
{
    FILE *f=fopen("/proc/sys/kernel/random/entropy_avail","r"); long count=-1; int saved=0;
    if(!f)saved=errno;
    else { if(fscanf(f,"%ld",&count)!=1)count=-1; fclose(f); }
    printf("stage=%s entropy_avail_bits=%ld open_errno=%d estimate_only=1\n",stage,count,saved);
}
int main(int argc,char **argv)
{
    struct rlimit cpu={60,60},core={0,0}; unsigned char digest[32];
    unsigned long began; int kdf=-1,cert=-1,do_kdf,do_cert,priority;
    char *args[]={"cost-probe","--selftest","/dev/null",0};
    if(argc!=2||(strcmp(argv[1],"--measure")&&strcmp(argv[1],"--kdf-only")&&strcmp(argv[1],"--entropy-only"))){fputs("usage: cost-probe --measure | --kdf-only | --entropy-only (separate target authorization required)\n",stderr);return 2;}
    setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
    if(setrlimit(RLIMIT_CPU,&cpu)||setrlimit(RLIMIT_CORE,&core)||setpriority(PRIO_PROCESS,0,10)){fprintf(stderr,"stage=bounds reason=setup errno=%d\n",errno);return 2;}
    errno=0;priority=getpriority(PRIO_PROCESS,0);
    if(errno||priority<10){fprintf(stderr,"stage=bounds reason=priority errno=%d nice=%d\n",errno,priority);return 2;}
    if(signal(SIGALRM,expired)==SIG_ERR||signal(SIGXCPU,expired)==SIG_ERR){fprintf(stderr,"stage=bounds reason=signal errno=%d\n",errno);return 2;}
    alarm(60);printf("probe_format=2 wall_bound_s=60 cpu_bound_s=60 nice=%d rounds=%u network=none credentials=none\n",priority,WEB_PASSWORD_ROUNDS);
    do_kdf=strcmp(argv[1],"--entropy-only")!=0;do_cert=strcmp(argv[1],"--kdf-only")!=0;
    if(do_kdf){
        phase=1;puts("stage=kdf state=started disposable=1");
        mbedtls_memory_buffer_alloc_init(arena.bytes,sizeof(arena.bytes));began=now_ms();
        kdf=mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,(const unsigned char*)"disposable-probe-password",sizeof("disposable-probe-password")-1,(const unsigned char*)"probe-salt-16byte",16,WEB_PASSWORD_ROUNDS,32,digest);
        {volatile unsigned char *p=digest;size_t i;for(i=0;i<sizeof(digest);i++)p[i]=0;}
        printf("stage=kdf state=finished rounds=%u elapsed_ms=%lu result=%d\n",WEB_PASSWORD_ROUNDS,now_ms()-began,kdf);
        usage_sample("kdf");mbedtls_memory_buffer_alloc_free();
    }
    if(do_cert){
        phase=2;entropy_count("entropy_before");puts("stage=certificate state=started");began=now_ms();
        cert=certificate_probe_main(3,args);
        printf("stage=certificate state=finished elapsed_ms=%lu result=%d entropy_calls=%u\n",now_ms()-began,cert,entropy_calls);
        entropy_count("entropy_after");usage_sample("certificate");
    }
    alarm(0);printf("stage=summary kdf=%s certificate=%s\n",do_kdf?(kdf?"fail":"pass"):"skipped",do_cert?(cert?"fail":"pass"):"skipped");
    return (do_kdf&&kdf)||(do_cert&&cert)?1:0;
}
