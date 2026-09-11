#ifndef FOURVRS_CORE_TRANSACTION_H
#define FOURVRS_CORE_TRANSACTION_H

#include "core/deadline.h"

#define CORE_TRANSACTION_PAYLOAD_MAX 256
#define CORE_TRANSPORT_METADATA_WORDS 4

typedef enum core_transaction_status {
    CORE_TX_QUEUED = 0,
    CORE_TX_ACTIVE,
    CORE_TX_COMPLETED,
    CORE_TX_TIMED_OUT,
    CORE_TX_CANCELLED,
    CORE_TX_FAILED,
    CORE_TX_MALFORMED
} core_transaction_status_t;

typedef enum core_operation_direction {
    CORE_OPERATION_TX_ONLY = 0,
    CORE_OPERATION_RX_ONLY,
    CORE_OPERATION_TX_THEN_RX
} core_operation_direction_t;

typedef enum core_response_policy {
    CORE_RESPONSE_NONE = 0,
    CORE_RESPONSE_FIXED_LENGTH,
    CORE_RESPONSE_BACKEND_FRAMED
} core_response_policy_t;

typedef struct core_operation {
    core_operation_direction_t direction;
    core_response_policy_t response_policy;
    unsigned int expected_response_length;
} core_operation_t;

typedef struct core_transaction {
    unsigned int id;
    unsigned int generation;
    unsigned int owner_kind;
    unsigned int owner_id;
    unsigned int transport_metadata[CORE_TRANSPORT_METADATA_WORDS];
    core_operation_t operation;
    core_tick_t enqueued_at;
    core_tick_t started_at;
    core_deadline_t queue_deadline;
    core_deadline_t response_deadline;
    core_tick_t response_timeout;
    unsigned int payload_length;
    unsigned char payload[CORE_TRANSACTION_PAYLOAD_MAX];
    core_transaction_status_t status;
    int cancelled;
    int request_transmitted;
} core_transaction_t;

#endif
