/* Host-only realtime substitution. Never linked into any delivery binary. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <time.h>
int clock_gettime(clockid_t clock,struct timespec *value)
{
    const char *stamp=getenv("NV_TEST_CLOCK");
    int (*real_clock)(clockid_t,struct timespec *);
    if(clock==CLOCK_REALTIME&&stamp){value->tv_sec=(time_t)strtoll(stamp,0,10);value->tv_nsec=0;return 0;}
    *(void **)(&real_clock)=dlsym(RTLD_NEXT,"clock_gettime");
    return real_clock(clock,value);
}
time_t time(time_t *out)
{
    struct timespec t;if(clock_gettime(CLOCK_REALTIME,&t))return (time_t)-1;
    if(out)*out=t.tv_sec;return t.tv_sec;
}
