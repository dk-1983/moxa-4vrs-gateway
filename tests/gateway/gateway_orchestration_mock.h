#ifndef FOURVRS_GATEWAY_ORCHESTRATION_MOCK_H
#define FOURVRS_GATEWAY_ORCHESTRATION_MOCK_H

#include "gateway/gateway_controller.h"

typedef struct gateway_transport_mock {
    unsigned int starts;
    unsigned int stops;
    unsigned int clients;
    unsigned int endpoint;
    unsigned int crc_error_count;
    unsigned int framing_error_count;
    int start_failures_remaining;
    int stop_failures_remaining;
    int active;
} gateway_transport_mock_t;

void gateway_transport_mock_init(gateway_transport_mock_t *mock);
const gateway_transport_ops_t *gateway_transport_mock_ops(void);

#endif
