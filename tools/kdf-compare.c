/* Disposable standalone comparison. No RNG, network, stored verifier or UART. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/pkcs5.h"
#include "web/web_security.h"
#include "kdf-cached.h"
#include "kdf-vectors.h"
static union {unsigned char bytes[524288];uint64_t alignment;} arena;
static void expired(int sig){const char msg[]="stage=kdf reason=bound_expired exit=124\n";(void)sig;if(write(2,msg,sizeof(msg)-1)<0){}_exit(124);}
static unsigned long now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)){perror("stage=clock");_exit(2);}return (unsigned long)t.tv_sec*1000UL+t.tv_nsec/1000000UL;}
static int derive(const unsigned char *p,size_t n,const unsigned char *s,unsigned int rounds,unsigned char *out){
#ifdef KDF_CACHED
 return cached_pbkdf2(p,n,s,rounds,out);
#else
 return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,p,n,s,16,rounds,32,out);
#endif
}
int main(int argc,char **argv)
{
 struct rlimit cpu={60,60},core={0,0};struct rusage usage;unsigned char out[32],other[32];unsigned long began,elapsed;size_t i;int r,priority;
 if(argc!=2||(strcmp(argv[1],"--measure")&&strcmp(argv[1],"--selftest"))){fputs("usage: kdf-compare --measure | --selftest\n",stderr);return 2;}
 setvbuf(stdout,NULL,_IONBF,0);
 if(setrlimit(RLIMIT_CPU,&cpu)||setrlimit(RLIMIT_CORE,&core)||setpriority(PRIO_PROCESS,0,10)){perror("stage=bounds");return 2;}
 errno=0;priority=getpriority(PRIO_PROCESS,0);if(errno||priority<10)return 2;
 if(signal(SIGALRM,expired)==SIG_ERR||signal(SIGXCPU,expired)==SIG_ERR)return 2;alarm(60);
 printf("probe_format=3 variant=%s rounds=%u nice=%d wall_bound_s=60 cpu_bound_s=60 entropy=not_used\n",KDF_VARIANT,WEB_PASSWORD_ROUNDS,priority);
 mbedtls_memory_buffer_alloc_init(arena.bytes,sizeof(arena.bytes));
 /* Independent Python/OpenSSL vectors, including >64-byte keys and UTF-8. */
 for(i=0;i<sizeof(vectors)/sizeof(vectors[0]);i++){
  r=derive(vectors[i].password,vectors[i].plen,vectors[i].salt,vectors[i].rounds,out);
  if(r||memcmp(out,vectors[i].expected,32)){printf("stage=vectors case=%lu result=fail\n",(unsigned long)i);return 1;}
  r=cached_pbkdf2(vectors[i].password,vectors[i].plen,vectors[i].salt,vectors[i].rounds,other);
  if(r||memcmp(other,out,32))return 1;
 }
 puts("stage=vectors result=pass");
 if(!strcmp(argv[1],"--selftest")){alarm(0);mbedtls_memory_buffer_alloc_free();return 0;}
 puts("stage=kdf state=started");began=now_ms();
 r=derive(measure_password,sizeof(measure_password),measure_salt,WEB_PASSWORD_ROUNDS,out);elapsed=now_ms()-began;
 if(!r&&memcmp(out,measure_expected,32))r=-1;
 memset(&usage,0,sizeof(usage));i=getrusage(RUSAGE_SELF,&usage)==0;
 printf("stage=kdf state=finished elapsed_ms=%lu rounds=%u result=%d vector=%s cpu_sample_valid=%u cpu_user_s=%ld.%06ld cpu_system_s=%ld.%06ld maxrss_native=%ld maxrss_units=unverified\n",elapsed,WEB_PASSWORD_ROUNDS,r,r?"fail":"pass",(unsigned)i,(long)usage.ru_utime.tv_sec,(long)usage.ru_utime.tv_usec,(long)usage.ru_stime.tv_sec,(long)usage.ru_stime.tv_usec,usage.ru_maxrss);
 mbedtls_platform_zeroize(out,sizeof(out));mbedtls_platform_zeroize(other,sizeof(other));mbedtls_memory_buffer_alloc_free();alarm(0);return r?1:0;
}
