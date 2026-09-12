#ifndef FOURVRS_MONOTONIC_H
#define FOURVRS_MONOTONIC_H
#include <time.h>
#ifndef FOURVRS_LINUX24
#define gateway_monotonic_time(t) clock_gettime(CLOCK_MONOTONIC,(t))
#else
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/times.h>
/* Linux 2.4 target only. times() measures elapsed ticks, not wall time.
 * Each caller must sample at least once per 32-bit tick wrap (~497 days
 * at the qualified 100 Hz). Single-threaded event-loop consumers only. */
static int gateway_monotonic_time(struct timespec *t)
{
 static uint32_t previous;
 static uint64_t total;
 static int initialized;
 struct tms usage;
 clock_t value;
 long hz=sysconf(_SC_CLK_TCK);
 uint32_t current;
 if(!t || hz!=100 || sizeof(clock_t)!=4){errno=EINVAL;return -1;}
 errno=0;value=times(&usage);
 if(value==(clock_t)-1 && errno)return -1;
 current=(uint32_t)value;
 if(!initialized){total=current;initialized=1;}
 else total+=(uint32_t)(current-previous);
 previous=current;
 t->tv_sec=(time_t)(total/100U);
 t->tv_nsec=(long)(total%100U)*10000000L;
 return 0;
}
#endif
#endif
