#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "app/gateway_application.h"

/* Hardware-only test of the production manager. Never run by host suites.
 * The configuration directory is read, never staged or promoted. */
static gateway_application_t app;
static volatile sig_atomic_t stopping;
static void stop(int signum){(void)signum;stopping=1;}
int main(int argc,char **argv)
{
    core_tick_t began,finished_at=0,last_result_at=0,last_start=0;
    unsigned enabled=0,done=0,failed=0,observed=0,results=0,total_before=0;
    gateway_time_trust_state_t previous=GATEWAY_TIME_UNKNOWN;
    if(argc!=3||strcmp(argv[2],"--confirm-three-ntp-attempts")!=0)return 2;
    setvbuf(stdout,0,_IONBF,0);signal(SIGTERM,stop);signal(SIGINT,stop);
    if(gateway_application_init_production(&app,argv[1])!=0)return 3;
    began=gateway_application_monotonic_ms(0);
    while(!gateway_application_finished(&app)){
        core_tick_t now=gateway_application_monotonic_ms(0);
        if(stopping||core_elapsed(began,now)>300000U){failed=1;gateway_application_request_stop(&app);}
        gateway_application_step(&app);
        if(!enabled&&app.coordinator.state==GATEWAY_APP_READY&&app.time.trust==GATEWAY_TIME_SYNCED){
            if(app.time.ntp_interval_hours!=1U||strcmp(app.time.ntp_server,"10.0.0.1")!=0||app.time.rtc!=GATEWAY_RTC_SAVED){failed=1;gateway_application_request_stop(&app);}
            else{
                total_before=app.time.ntp_attempts;
                if(gateway_application_ntp_test(&app,1U)!=0){failed=1;gateway_application_request_stop(&app);}
                else{enabled=1;last_result_at=now;
                    printf("TEST_ENABLED now=%lu next=%lu ordinary_hours=%u initial_sync=%lu rtc=%s\n",(unsigned long)now,(unsigned long)app.time.next_ntp_attempt,app.time.ntp_interval_hours,(unsigned long)app.time.last_sync,gateway_rtc_state_name(app.time.rtc));}
            }
        }
        if(enabled&&app.time.ntp_test_attempts!=observed){
            core_tick_t gap=core_elapsed(last_result_at,app.time.last_ntp_attempt);
            observed=app.time.ntp_test_attempts;last_start=app.time.last_ntp_attempt;
            printf("ATTEMPT=%u start=%lu gap_after_result=%lu\n",observed,(unsigned long)last_start,(unsigned long)gap);
            if(observed>3U||gap<60000U||gap>61000U)failed=1;
        }
        if(enabled&&observed>results&&previous==GATEWAY_TIME_SYNCING&&app.time.trust!=GATEWAY_TIME_SYNCING){
            ++results;last_result_at=now;
            printf("RESULT=%u now=%lu result=%u trust=%u rtc=%s active=%u next=%lu last_sync=%lu\n",results,(unsigned long)now,app.time.last_ntp_result,app.time.trust,gateway_rtc_state_name(app.time.rtc),app.time.ntp_test_active,(unsigned long)app.time.next_ntp_attempt,(unsigned long)app.time.last_sync);
            if(app.time.last_ntp_result!=GATEWAY_NTP_SUCCEEDED||app.time.rtc!=GATEWAY_RTC_SAVED)failed=1;
        }
        previous=app.time.trust;
        if(enabled&&!done&&!app.time.ntp_test_active&&observed==3U){
            done=1;finished_at=now;
            if(results!=3U||app.time.ntp_attempts!=total_before+3U||app.time.next_ntp_attempt-app.time.last_sync!=3600000U)failed=1;
            printf("AUTO_END results=%u attempts=%u active=%u hours=%u next_minus_sync=%lu\n",results,observed,app.time.ntp_test_active,app.time.ntp_interval_hours,(unsigned long)(app.time.next_ntp_attempt-app.time.last_sync));
        }
        if(done&&core_elapsed(finished_at,now)>=5000U)gateway_application_request_stop(&app);
        gateway_application_wait_default(0,1000);
    }
    printf("NTP_INTERVAL_%s stopped=%u exit=%d\n",done&&!failed?"PASS":"FAIL",gateway_application_finished(&app),app.exit_status);
    return failed||!done||app.exit_status?1:0;
}
