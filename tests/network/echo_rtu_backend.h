#ifndef FOURVRS_TEST_ECHO_RTU_BACKEND_H
#define FOURVRS_TEST_ECHO_RTU_BACKEND_H

#include "core/port_runtime.h"

typedef struct echo_rtu_backend {
    backend_event_t event;
    int pending;
    unsigned int starts;
    unsigned int suppress_responses;
    int transmit_pending;
    unsigned int active_generation;
} echo_rtu_backend_t;

void echo_rtu_backend_init(echo_rtu_backend_t *backend);
const core_backend_ops_t *echo_rtu_backend_ops(void);
void echo_rtu_backend_suppress(echo_rtu_backend_t *backend,
                               unsigned int response_count);

#endif
