#include "modbus/modbus_crc.h"
#include "modbus/modbus_tcp_adapter.h"

core_submit_result_t modbus_tcp_submit(port_runtime_t *port,
                                       const modbus_tcp_request_t *request,
                                       core_tick_t now,
                                       core_tick_t queue_timeout,
                                       core_tick_t response_timeout,
                                       unsigned int *core_transaction_id)
{
    unsigned char rtu[MODBUS_RTU_ADU_MAX];
    unsigned int metadata[CORE_TRANSPORT_METADATA_WORDS];
    unsigned int length;
    unsigned short crc;
    core_operation_t operation;
    if (request == 0 || request->adu.pdu_length == 0 ||
        request->adu.pdu_length > MBAP_PDU_MAX)
        return CORE_INVALID_REQUEST;
    length = 1U + request->adu.pdu_length;
    if (length + 2U > MODBUS_RTU_ADU_MAX)
        return CORE_INVALID_REQUEST;
    rtu[0] = request->adu.unit_id;
    {
        unsigned int i;
        for (i = 0; i < request->adu.pdu_length; ++i)
            rtu[1U + i] = request->adu.pdu[i];
    }
    crc = modbus_crc16(rtu, length);
    rtu[length++] = (unsigned char)(crc & 0xffU);
    rtu[length++] = (unsigned char)(crc >> 8);
    metadata[MODBUS_META_TRANSACTION_ID] = request->adu.transaction_id;
    metadata[MODBUS_META_UNIT_FUNCTION] =
        ((unsigned int)request->adu.unit_id << 8) | request->adu.pdu[0];
    metadata[MODBUS_META_CLIENT_EPOCH] = request->client_epoch;
    metadata[MODBUS_META_RESERVED] = 0;
    operation.direction = CORE_OPERATION_TX_THEN_RX;
    operation.response_policy = CORE_RESPONSE_BACKEND_FRAMED;
    operation.expected_response_length = 0;
    return port_runtime_submit_operation(
        port, rtu, length, MODBUS_OWNER_TCP, request->client_id, metadata,
        &operation, now,
        queue_timeout, response_timeout, core_transaction_id);
}

uart_frame_result_t modbus_rtu_frame_probe(
    void *context, const unsigned char *request, unsigned int request_length,
    const unsigned char *response, unsigned int response_length,
    unsigned int *frame_length)
{
    unsigned int expected;
    unsigned int function;
    (void)context;
    if (request == 0 || response == 0 || frame_length == 0 ||
        request_length < 4U || request_length > MODBUS_RTU_ADU_MAX ||
        response_length > MODBUS_RTU_ADU_MAX)
        return UART_FRAME_MALFORMED;
    if (response_length < 2U)
        return UART_FRAME_INCOMPLETE;
    if (response[0] != request[0])
        return UART_FRAME_MALFORMED;
    function = request[1];
    if (response[1] == (unsigned char)(function | 0x80U)) {
        expected = 5U;
    } else if (response[1] != function) {
        return UART_FRAME_MALFORMED;
    } else if (function >= 1U && function <= 4U) {
        if (response_length < 3U)
            return UART_FRAME_INCOMPLETE;
        expected = 5U + response[2];
        if (expected > MODBUS_RTU_ADU_MAX)
            return UART_FRAME_MALFORMED;
    } else if (function == 5U || function == 6U ||
               function == 15U || function == 16U) {
        expected = 8U;
    } else {
        return UART_FRAME_MALFORMED;
    }
    *frame_length = expected;
    if (response_length < expected)
        return UART_FRAME_INCOMPLETE;
    if (response_length != expected)
        return UART_FRAME_MALFORMED;
    {
        unsigned short actual = (unsigned short)(response[expected - 2U] |
            ((unsigned short)response[expected - 1U] << 8));
        if (modbus_crc16(response, expected - 2U) != actual)
            return UART_FRAME_MALFORMED;
    }
    return UART_FRAME_COMPLETE;
}

int modbus_tcp_build_response(const core_transaction_t *transaction,
                              const unsigned char *rtu, unsigned int rtu_length,
                              unsigned char *output, unsigned int capacity,
                              unsigned int *output_length)
{
    unsigned int pdu_length;
    unsigned int length_field;
    unsigned short expected;
    unsigned short actual;
    unsigned int metadata;
    unsigned int i;
    if (transaction == 0 || rtu == 0 || output == 0 || output_length == 0 ||
        rtu_length < 4U || rtu_length > MODBUS_RTU_ADU_MAX)
        return -1;
    expected = modbus_crc16(rtu, rtu_length - 2U);
    actual = (unsigned short)(rtu[rtu_length - 2U] |
                              ((unsigned short)rtu[rtu_length - 1U] << 8));
    if (expected != actual)
        return -2;
    metadata = transaction->transport_metadata[MODBUS_META_UNIT_FUNCTION];
    if (rtu[0] != (unsigned char)(metadata >> 8))
        return -3;
    if (rtu[1] != (unsigned char)(metadata & 0xffU) &&
        rtu[1] != (unsigned char)((metadata & 0xffU) | 0x80U))
        return -4;
    pdu_length = rtu_length - 3U;
    length_field = 1U + pdu_length;
    if (7U + pdu_length > capacity)
        return -5;
    output[0] = (unsigned char)(transaction->transport_metadata[0] >> 8);
    output[1] = (unsigned char)transaction->transport_metadata[0];
    output[2] = 0;
    output[3] = 0;
    output[4] = (unsigned char)(length_field >> 8);
    output[5] = (unsigned char)length_field;
    output[6] = rtu[0];
    for (i = 0; i < pdu_length; ++i)
        output[7U + i] = rtu[1U + i];
    *output_length = 7U + pdu_length;
    return 0;
}

int modbus_tcp_build_gateway_exception(
    const core_transaction_t *transaction, unsigned int exception_code,
    unsigned char *output, unsigned int capacity,
    unsigned int *output_length)
{
    unsigned int metadata;
    if (transaction == 0 || output == 0 || output_length == 0 ||
        capacity < 9U || (exception_code != MODBUS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE &&
        exception_code != MODBUS_EXCEPTION_GATEWAY_TARGET_NO_RESPONSE))
        return -1;
    metadata = transaction->transport_metadata[MODBUS_META_UNIT_FUNCTION];
    output[0] = (unsigned char)(transaction->transport_metadata[MODBUS_META_TRANSACTION_ID] >> 8);
    output[1] = (unsigned char)transaction->transport_metadata[MODBUS_META_TRANSACTION_ID];
    output[2] = 0;
    output[3] = 0;
    output[4] = 0;
    output[5] = 3;
    output[6] = (unsigned char)(metadata >> 8);
    output[7] = (unsigned char)((metadata & 0xffU) | 0x80U);
    output[8] = (unsigned char)exception_code;
    *output_length = 9U;
    return 0;
}
