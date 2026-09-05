#ifndef FOURVRS_UART_ANOMALY_CONTROL_H
#define FOURVRS_UART_ANOMALY_CONTROL_H

#include <signal.h>

typedef enum anomaly_test_phase {
    ANOMALY_PHASE_IDLE = 0,
    ANOMALY_PHASE_P1_TO_P2,
    ANOMALY_PHASE_P2_TO_P1,
    ANOMALY_PHASE_SPACING,
    ANOMALY_PHASE_PARTIAL_READ,
    ANOMALY_PHASE_CLEANUP
} anomaly_test_phase_t;

typedef struct anomaly_run_control {
    anomaly_test_phase_t phase;
    unsigned int cleanup_count;
    unsigned int intentional_stop;
} anomaly_run_control_t;

int anomaly_control_install(void);
void anomaly_control_reset(anomaly_run_control_t *control);
void anomaly_control_request_for_test(void);
int anomaly_control_stop_requested(anomaly_run_control_t *control);
int anomaly_control_begin_cleanup(anomaly_run_control_t *control);

#endif
