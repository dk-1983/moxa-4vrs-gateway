#include <signal.h>
#include <stdio.h>

#include "uart_anomaly_control.h"

static unsigned int checks;
static unsigned int failures;

#define CHECK(value) do { checks++; if (!(value)) { failures++; \
    printf("FAIL line=%u\n",(unsigned int)__LINE__); } } while (0)

static void cancellation_phase(anomaly_test_phase_t phase)
{
    anomaly_run_control_t control;
    anomaly_control_reset(&control); control.phase=phase;
    anomaly_control_request_for_test();
    CHECK(anomaly_control_stop_requested(&control));
    CHECK(control.intentional_stop==1U);
    CHECK(anomaly_control_begin_cleanup(&control));
    CHECK(control.phase==ANOMALY_PHASE_CLEANUP);
    CHECK(control.cleanup_count==1U);
    CHECK(!anomaly_control_begin_cleanup(&control));
    CHECK(control.cleanup_count==1U);
}

int main(void)
{
    anomaly_run_control_t control;
    unsigned int completed=37123U,bytes=2865017U,restores=0,closes=0;
    CHECK(anomaly_control_install()==0);
    cancellation_phase(ANOMALY_PHASE_P1_TO_P2);
    cancellation_phase(ANOMALY_PHASE_P2_TO_P1);
    cancellation_phase(ANOMALY_PHASE_SPACING);
    cancellation_phase(ANOMALY_PHASE_PARTIAL_READ);

    anomaly_control_reset(&control); control.phase=ANOMALY_PHASE_P1_TO_P2;
    anomaly_control_request_for_test();
    CHECK(anomaly_control_stop_requested(&control));
    CHECK(completed==37123U); CHECK(bytes==2865017U);
    if(anomaly_control_begin_cleanup(&control)) {
        restores+=2U; closes+=2U;
    }
    CHECK(restores==2U); CHECK(closes==2U);
    CHECK(!anomaly_control_begin_cleanup(&control));
    CHECK(restores==2U); CHECK(closes==2U);
    CHECK(control.intentional_stop==1U);

    anomaly_control_reset(&control);
    CHECK(!anomaly_control_stop_requested(&control));
    CHECK(control.intentional_stop==0U);
    printf("uart_anomaly_control checks=%u failures=%u\n",checks,failures);
    return failures==0U?0:1;
}
