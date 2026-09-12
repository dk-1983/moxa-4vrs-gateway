#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/times.h>
#include <assert.h>
#include <stdio.h>
static uint32_t simulated;
static int fail;
static clock_t fake_times(struct tms *unused){(void)unused;if(fail){errno=EFAULT;return (clock_t)-1;}return (clock_t)simulated;}
static long fake_sysconf(int unused){(void)unused;return 100;}
#define times fake_times
#define sysconf fake_sysconf
#define FOURVRS_LINUX24
#include "core/monotonic.h"
int main(void){struct timespec t;uint64_t last,now;
 simulated=0xfffffffeU;assert(!gateway_monotonic_time(&t));last=(uint64_t)t.tv_sec*100+t.tv_nsec/10000000;
 simulated=0xffffffffU;assert(!gateway_monotonic_time(&t));now=(uint64_t)t.tv_sec*100+t.tv_nsec/10000000;assert(now==last+1);last=now;
 simulated=0;assert(!gateway_monotonic_time(&t));now=(uint64_t)t.tv_sec*100+t.tv_nsec/10000000;assert(now==last+1);last=now;
 fail=1;assert(gateway_monotonic_time(&t)==-1&&errno==EFAULT);fail=0;
 simulated=200;assert(!gateway_monotonic_time(&t));now=(uint64_t)t.tv_sec*100+t.tv_nsec/10000000;assert(now==last+200);
 puts("linux24 monotonic: wrap, minus-one tick, failure, elapsed conversion PASS");return 0;}
