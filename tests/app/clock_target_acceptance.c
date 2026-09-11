#define _POSIX_C_SOURCE 199309L
#include <fcntl.h>
#include <linux/rtc.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "app/gateway_application.h"

/* Explicit hardware-only harness: never included in host suites. Uses the
 * production application manager and providers, without panel interaction. */
static gateway_application_t application;
static volatile sig_atomic_t stopping;
static void stop(int signal_number){(void)signal_number;stopping=1;}
int main(int argc,char **argv)
{
    unsigned tested=0;int failed=0;
    if(argc!=3||strcmp(argv[2],"--confirm-manual-rtc-write")!=0)return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);setvbuf(stdout,0,_IONBF,0);
    if(gateway_application_init_production(&application,argv[1])!=0)return 3;
    while(!gateway_application_finished(&application)){
        if(stopping)gateway_application_request_stop(&application);
        gateway_application_step(&application);
        if(!tested&&application.coordinator.state==GATEWAY_APP_READY){
            gateway_system_time_t local;struct rtc_time rtc;struct tm expected;
            time_t before,after;int fd,result;
            tested=1;printf("READY rtc_probe=%s\n",gateway_rtc_state_name(application.time.rtc));
            before=time(0);expected=*gmtime(&before);
            result=gateway_application_system_time_get(&application,&local);
            if(result==GATEWAY_SYSTEM_TIME_OK)result=gateway_application_system_time_set(&application,&local);
            fd=open("/dev/rtc",O_RDONLY);memset(&rtc,0,sizeof(rtc));
            if(fd<0||ioctl(fd,RTC_RD_TIME,&rtc)!=0)failed=1;
            if(fd>=0)close(fd);
            after=time(0);
            printf("manual_result=%d trust=%d rtc=%s before=%ld after=%ld readback=%04d-%02d-%02dT%02d:%02d:%02dZ\n",result,application.time.trust,gateway_rtc_state_name(application.time.rtc),(long)before,(long)after,rtc.tm_year+1900,rtc.tm_mon+1,rtc.tm_mday,rtc.tm_hour,rtc.tm_min,rtc.tm_sec);
            if(result!=GATEWAY_SYSTEM_TIME_OK||application.time.trust!=GATEWAY_TIME_MANUAL||application.time.rtc!=GATEWAY_RTC_SAVED||after-before>1||after<before)failed=1;
            /* Retry only the read on a second boundary; never repeat a write. */
            if(rtc.tm_year!=expected.tm_year||rtc.tm_mon!=expected.tm_mon||rtc.tm_mday!=expected.tm_mday||rtc.tm_hour!=expected.tm_hour||rtc.tm_min!=expected.tm_min||rtc.tm_sec!=expected.tm_sec){
                expected=*gmtime(&after);
                if(rtc.tm_year!=expected.tm_year||rtc.tm_mon!=expected.tm_mon||rtc.tm_mday!=expected.tm_mday||rtc.tm_hour!=expected.tm_hour||rtc.tm_min!=expected.tm_min||rtc.tm_sec!=expected.tm_sec)failed=1;
            }
            printf("MANUAL_RTC_%s\n",failed?"FAIL":"PASS");
        }
        gateway_application_wait_default(0,1000);
    }
    printf("STOPPED exit=%d rtc_failures=%u\n",application.exit_status,application.time.rtc_save_failures);
    return failed||!tested||application.exit_status?1:0;
}
