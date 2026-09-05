#ifndef FOURVRS_MODBUS_TCP_ADAPTER_H
#define FOURVRS_MODBUS_TCP_ADAPTER_H

#include "core/port_runtime.h"
#include "modbus/mbap_stream.h"
#include "uart/uart_backend.h"

#define MODBUS_OWNER_TCP 1U
#define MODBUS_META_TRANSACTION_ID 0U
#define MODBUS_META_UNIT_FUNCTION 1U
#define MODBUS_META_CLIENT_EPOCH 2U
#define MODBUS_META_RESERVED 3U
#define MODBUS_RTU_ADU_MAX 256U
#define MODBUS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE 0x0AU
#define MODBUS_EXCEPTION_GATEWAY_TARGET_NO_RESPONSE 0x0BU

typedef struct modbus_tcp_request {
    unsigned int client_id;
    unsigned int client_epoch;
    mbap_adu_t adu;
} modbus_tcp_request_t;

core_submit_result_t modbus_tcp_submit(port_runtime_t *port,
                                       const modbus_tcp_request_t *request,
                                       core_tick_t now,
                                       core_tick_t queue_timeout,
                                       core_tick_t response_timeout,
                                       unsigned int *core_transaction_id);

int modbus_tcp_build_response(const core_transaction_t *transaction,
                              const unsigned char *rtu, unsigned int rtu_length,
                              unsigned char *output, unsigned int capacity,
                              unsigned int *output_length);
int modbus_tcp_build_gateway_exception(
    const core_transaction_t *transaction, unsigned int exception_code,
    unsigned char *output, unsigned int capacity,
    unsigned int *output_length);
uart_frame_result_t modbus_rtu_frame_probe(
    void *context, const unsigned char *request, unsigned int request_length,
    const unsigned char *response, unsigned int response_length,
    unsigned int *frame_length);

#endif
