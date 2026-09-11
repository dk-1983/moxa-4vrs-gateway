#define _POSIX_C_SOURCE 200809L
#include "web/web_budget.h"
#include <time.h>
#include <sys/select.h>
#include <stdio.h>
#include <assert.h>
static uint32_t now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint32_t)t.tv_sec*1000U+(uint32_t)t.tv_nsec/1000000U;}
int main(void){web_budget_t b={0};uint32_t start=now(),cpu=web_cpu_us(),peak=0;volatile unsigned int sum=0;unsigned int delay,j;
 while(now()-start<2000U){delay=web_budget_delay(&b,now(),web_cpu_us());if(delay){struct timeval t={0,(delay>20?20:delay)*1000U};select(0,0,0,0,&t);}else{
  uint32_t before=web_cpu_us(),spent;for(j=0;j<500000;j++)sum+=j;spent=web_cpu_us()-before;if(spent>peak)peak=spent;
 }}
 cpu=web_cpu_us()-cpu;printf("wall_ms=%u cpu_us=%u largest_indivisible_step_us=%u burst_credit_us=%u accumulator=%u\n",now()-start,cpu,peak,WEB_CPU_BURST_US,sum);
 assert(cpu<1200000U);return 0;
}
