#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "app/gateway_application.h"

/* Linker wrappers ensure no host clock, RTC, or real ntpdate is touched. */
static unsigned checks,failed,writes,sets;
static int open_error,io_error,child_mode,extra_fd;
static time_t epoch=1788609600;
static struct rtc_time calendar,saved;
#define CHECK(x) do {++checks;if(!(x)){++failed;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int __real_open(const char *,int,...);
int __real_close(int);
int __wrap_open(const char *path,int flags,...)
{
    if(strcmp(path,"/dev/rtc")==0){if(open_error){errno=open_error;return -1;}return 7777;}
    /* Production clock helpers only open existing files. */
    if(flags&O_CREAT){errno=EPERM;return -1;}
    return __real_open(path,flags);
}
int __wrap_close(int fd){return fd==7777?0:__real_close(fd);}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    va_list args;struct rtc_time *value;
    if(fd!=7777){errno=EPERM;return -1;}
    va_start(args,request);value=va_arg(args,struct rtc_time *);va_end(args);
    if(io_error){errno=EIO;return -1;}
    if(request==RTC_RD_TIME){*value=calendar;return 0;}
    if(request==RTC_SET_TIME){saved=*value;++writes;return 0;}
    errno=EINVAL;return -1;
}
time_t __wrap_time(time_t *out){if(out)*out=epoch;return epoch;}
int __wrap_clock_settime(clockid_t id,const struct timespec *value)
{if(id!=CLOCK_REALTIME){errno=EINVAL;return -1;}++sets;epoch=value->tv_sec;return 0;}
int __wrap_execl(const char *path,const char *arg,...)
{
    char buffer[16384];const char *server;va_list args;size_t used=0;
    va_start(args,arg);server=va_arg(args,const char *);va_end(args);
    if(strcmp(path,"/usr/sbin/ntpdate")||strcmp(arg,"ntpdate")||strcmp(server,"fake.test"))_exit(90);
    if(fcntl(extra_fd,F_GETFD)!=-1||errno!=EBADF)_exit(91);
    if(child_mode){for(;;)pause();}
    memset(buffer,'x',sizeof(buffer));
    while(used<sizeof(buffer)){ssize_t n=write(1,buffer+used,sizeof(buffer)-used);if(n<=0)_exit(92);used+=(size_t)n;}
    _exit(0);
}
static int reap(gateway_application_dependencies_t *d)
{unsigned i;int result=0;for(i=0;i<5000U&&result==0;++i){result=d->ntp_poll(d->ntp_context);usleep(1000);}return result;}
int main(void)
{
    gateway_application_t a;gateway_application_dependencies_t d;
    gateway_system_time_t v={2026,9,5,19,0,0};struct rlimit original,lower;
    int pid,status;unsigned before;struct tm *utc;
    memset(&a,0,sizeof(a));a.ntp_output_fd=-1;
    gateway_application_dependencies_production(&d,&a,0);
    calendar.tm_year=126;calendar.tm_mon=8;calendar.tm_mday=5;
    CHECK(d.rtc_probe(0)==GATEWAY_RTC_UNVERIFIED);
    calendar.tm_mon=1;calendar.tm_mday=29;CHECK(d.rtc_probe(0)==GATEWAY_RTC_INVALID);
    io_error=1;CHECK(d.rtc_probe(0)==GATEWAY_RTC_READ_FAILED);io_error=0;
    open_error=ENOENT;CHECK(d.rtc_probe(0)==GATEWAY_RTC_UNAVAILABLE);
    open_error=EACCES;CHECK(d.rtc_probe(0)==GATEWAY_RTC_READ_FAILED);open_error=0;
    CHECK(setenv("TZ","UTC-7",1)==0);tzset();
    CHECK(gateway_system_time_set_default(0,&v)==GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED&&sets==1);
    CHECK(d.rtc_save(0)==GATEWAY_RTC_SAVED&&writes==1);
    utc=gmtime(&epoch);CHECK(saved.tm_hour==12&&saved.tm_hour==utc->tm_hour&&saved.tm_mday==5);
    io_error=1;CHECK(d.rtc_save(0)==GATEWAY_RTC_WRITE_FAILED);io_error=0;
    epoch=-1;before=writes;CHECK(d.rtc_save(0)==GATEWAY_RTC_WRITE_FAILED&&writes==before);
    CHECK(setenv("TZ","EST5EDT,M3.2.0/2,M11.1.0/2",1)==0);tzset();
    v=(gateway_system_time_t){2026,3,8,2,30,0};
    CHECK(gateway_system_time_set_default(0,&v)==GATEWAY_SYSTEM_TIME_INVALID&&sets==1);
    extra_fd=fcntl(0,F_DUPFD,100);CHECK(extra_fd>=100);
    CHECK(getrlimit(RLIMIT_NOFILE,&original)==0);lower=original;lower.rlim_cur=64;
    CHECK(setrlimit(RLIMIT_NOFILE,&lower)==0);
    CHECK(d.ntp_start(&a,"fake.test")==0);pid=a.ntp_helper_pid;
    CHECK(setrlimit(RLIMIT_NOFILE,&original)==0);
    CHECK(d.ntp_start(&a,"fake.test")==-1);
    CHECK(reap(&d)==1&&a.ntp_output_used==sizeof(a.ntp_output)-1U&&a.ntp_output_fd==-1);
    CHECK(waitpid(pid,&status,WNOHANG)==-1&&errno==ECHILD);
    child_mode=1;CHECK(d.ntp_start(&a,"fake.test")==0);pid=a.ntp_helper_pid;
    d.ntp_cancel(&a);CHECK(reap(&d)==-1&&a.ntp_helper_pid==0&&a.ntp_output_fd==-1);
    CHECK(waitpid(pid,&status,WNOHANG)==-1&&errno==ECHILD);close(extra_fd);
    printf("clock provider checks=%u failed=%u (clock/RTC/exec wrapped)\n",checks,failed);
    return failed?1:0;
}
