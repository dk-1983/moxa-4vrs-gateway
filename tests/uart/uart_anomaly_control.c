#include <string.h>

#include "uart_anomaly_control.h"

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

int anomaly_control_install(void)
{
    if (signal(SIGINT, request_stop) == SIG_ERR)
        return -1;
    if (signal(SIGTERM, request_stop) == SIG_ERR)
        return -1;
    return 0;
}

void anomaly_control_reset(anomaly_run_control_t *control)
{
    stop_requested = 0;
    memset(control, 0, sizeof(*control));
}

void anomaly_control_request_for_test(void)
{
    stop_requested = 1;
}

int anomaly_control_stop_requested(anomaly_run_control_t *control)
{
    if (!stop_requested)
        return 0;
    control->intentional_stop = 1;
    return 1;
}

int anomaly_control_begin_cleanup(anomaly_run_control_t *control)
{
    if (control->cleanup_count != 0)
        return 0;
    control->cleanup_count = 1;
    control->phase = ANOMALY_PHASE_CLEANUP;
    return 1;
}
