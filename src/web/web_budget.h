/* Cooperative Web-process CPU pacing for Linux 2.6.10. Not a scheduler quota.
 * The process can exceed its credit inside one indivisible crypto/syscall step.
 * getrusage counts user+system CPU, including startup TLS/certificate work.
 * Helpers and Gateway are intentionally NOT charged as if they were Web CPU. */
#ifndef FOURVRS_WEB_BUDGET_H
#define FOURVRS_WEB_BUDGET_H
#include <sys/resource.h>
#include <stdint.h>
typedef struct web_budget {uint32_t wall,cpu,peak_debit_us;int64_t credit;unsigned int initialized;} web_budget_t;
#define WEB_CPU_PERCENT 50U
#define WEB_CPU_BURST_US 20000U
static uint32_t web_cpu_us(void){struct rusage r;if(getrusage(RUSAGE_SELF,&r))return 0;return (uint32_t)((uint64_t)r.ru_utime.tv_sec*1000000U+r.ru_utime.tv_usec+(uint64_t)r.ru_stime.tv_sec*1000000U+r.ru_stime.tv_usec);}
static unsigned int web_budget_delay(web_budget_t*b,uint32_t wall,uint32_t cpu){
 uint32_t elapsed=wall-b->wall,spent=cpu-b->cpu;
 if(!b->initialized){b->initialized=1;b->wall=wall;b->cpu=cpu;b->credit=WEB_CPU_BURST_US;return 0;}
 b->wall=wall;b->cpu=cpu;
 if(spent>b->peak_debit_us)b->peak_debit_us=spent;
 b->credit+=(int64_t)elapsed*1000U*WEB_CPU_PERCENT/100U;
 if(b->credit>WEB_CPU_BURST_US)b->credit=WEB_CPU_BURST_US;
 b->credit-=spent;
 if(b->credit<0){uint64_t ms=(uint64_t)(-b->credit)*100U/(WEB_CPU_PERCENT*1000U)+1U;return ms>1000U?1000U:(unsigned int)ms;}
 return 0;
}
#endif
