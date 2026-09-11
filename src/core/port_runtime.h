#ifndef FOURVRS_CORE_PORT_RUNTIME_H
#define FOURVRS_CORE_PORT_RUNTIME_H

#include "core/transaction.h"

#define CORE_PORT_COUNT 8
#define CORE_QUEUE_CAPACITY 8
#define CORE_QUEUE_BYTES_MAX 1024
#define CORE_RECOVERY_MAX_ATTEMPTS 3
#define CORE_ERROR_LENGTH 32

typedef enum port_state {
    PORT_DISABLED = 0,
    PORT_STARTING,
    PORT_READY,
    PORT_ACTIVE,
    PORT_WAITING,
    PORT_RECOVERING,
    PORT_DEGRADED,
    PORT_RECONFIGURING,
    PORT_STOPPING,
    PORT_ERROR,
    PORT_STATE_COUNT
} port_state_t;

typedef enum core_submit_result {
    CORE_ACCEPTED = 0,
    CORE_QUEUE_FULL,
    CORE_PORT_DISABLED,
    CORE_PORT_RECOVERING,
    CORE_INVALID_REQUEST,
    CORE_RECONFIGURING
} core_submit_result_t;

typedef enum backend_event_type {
    BACKEND_EVENT_NONE = 0,
    BACKEND_EVENT_RESPONSE,
    BACKEND_EVENT_MALFORMED,
    BACKEND_EVENT_FAILURE,
    BACKEND_EVENT_TRANSMITTED
} backend_event_type_t;

typedef struct backend_event {
    backend_event_type_t type;
    unsigned int generation;
    unsigned int length;
    unsigned char data[CORE_TRANSACTION_PAYLOAD_MAX];
} backend_event_t;

typedef struct core_port_config {
    unsigned int revision;
    unsigned int mode;
    unsigned int baud;
    unsigned int data_bits;
    unsigned int parity;
    unsigned int stop_bits;
    unsigned int special_baud_enabled;
    unsigned int special_baud;
    unsigned int transport;
    unsigned int endpoint_port;
} core_port_config_t;

typedef struct core_backend_ops {
    int (*open)(void *context, core_tick_t now);
    int (*start)(void *context, const core_transaction_t *transaction,
                 core_tick_t now);
    int (*poll)(void *context, core_tick_t now, backend_event_t *event);
    void (*cancel)(void *context, unsigned int generation);
    int (*recover)(void *context, core_tick_t now);
    int (*reconfigure)(void *context, const core_port_config_t *config,
                       core_tick_t now);
    int (*stop)(void *context, core_tick_t now);
} core_backend_ops_t;

typedef void (*core_completion_fn)(void *context,
                                   const core_transaction_t *transaction,
                                   core_transaction_status_t status,
                                   const unsigned char *response,
                                   unsigned int response_length);

typedef struct port_statistics {
    unsigned int accepted;
    unsigned int completed;
    unsigned int timeouts;
    unsigned int cancelled;
    unsigned int queue_full;
    unsigned int invalid;
    unsigned int recoveries;
    unsigned int recovery_failures;
    unsigned int stale_responses;
    unsigned int backend_failures;
    unsigned int reconfigurations;
    unsigned int rollbacks;
    unsigned int illegal_transitions;
    unsigned int queue_depth;
    unsigned int queue_high_water;
    core_tick_t last_success_at;
} port_statistics_t;

typedef struct transaction_queue {
    core_transaction_t entries[CORE_QUEUE_CAPACITY];
    unsigned int head;
    unsigned int count;
    unsigned int bytes;
} transaction_queue_t;

typedef struct port_runtime {
    unsigned int port_index;
    port_state_t state;
    core_port_config_t current_config;
    core_port_config_t staged_config;
    core_port_config_t known_good_config;
    transaction_queue_t queue;
    core_transaction_t active;
    int has_active;
    unsigned int next_transaction_id;
    unsigned int generation;
    unsigned int response_barrier_generation;
    core_deadline_t state_deadline;
    core_tick_t recovery_delay;
    unsigned int recovery_attempts;
    char last_error[CORE_ERROR_LENGTH];
    port_statistics_t stats;
    const core_backend_ops_t *backend;
    void *backend_context;
    core_completion_fn completion;
    void *completion_context;
} port_runtime_t;

void port_runtime_init(port_runtime_t *port, unsigned int port_index,
                       const core_port_config_t *config,
                       const core_backend_ops_t *backend, void *backend_context);
void port_runtime_set_completion(port_runtime_t *port,
                                 core_completion_fn completion,
                                 void *completion_context);
int port_runtime_transition(port_runtime_t *port, port_state_t next);
int port_runtime_start(port_runtime_t *port, core_tick_t now,
                       core_tick_t start_timeout);
core_submit_result_t port_runtime_submit(port_runtime_t *port,
                                         const unsigned char *payload,
                                         unsigned int length,
                                         unsigned int owner_kind,
                                         unsigned int owner_id,
                                         core_tick_t now,
                                         core_tick_t queue_timeout,
                                         core_tick_t response_timeout,
                                         unsigned int *transaction_id);
core_submit_result_t port_runtime_submit_with_metadata(
    port_runtime_t *port, const unsigned char *payload, unsigned int length,
    unsigned int owner_kind, unsigned int owner_id,
    const unsigned int metadata[CORE_TRANSPORT_METADATA_WORDS],
    core_tick_t now, core_tick_t queue_timeout, core_tick_t response_timeout,
    unsigned int *transaction_id);
core_submit_result_t port_runtime_submit_operation(
    port_runtime_t *port, const unsigned char *payload, unsigned int length,
    unsigned int owner_kind, unsigned int owner_id,
    const unsigned int metadata[CORE_TRANSPORT_METADATA_WORDS],
    const core_operation_t *operation,
    core_tick_t now, core_tick_t queue_timeout, core_tick_t response_timeout,
    unsigned int *transaction_id);
void port_runtime_step(port_runtime_t *port, core_tick_t now);
int port_runtime_cancel_active(port_runtime_t *port, core_tick_t now);
/* Complete old listener callbacks before its context can be reused. */
int port_runtime_detach_listener(port_runtime_t *port, core_tick_t now);
/* Idle stream transports report device I/O failures through normal recovery. */
int port_runtime_report_io_failure(port_runtime_t *port, core_tick_t now,
                                   const char *message);
int port_runtime_request_reconfigure(port_runtime_t *port,
                                     const core_port_config_t *config,
                                     core_tick_t now,
                                     core_tick_t timeout);
int port_runtime_request_stop(port_runtime_t *port, core_tick_t now,
                              core_tick_t timeout);
core_tick_t port_runtime_active_age(const port_runtime_t *port,
                                    core_tick_t now);
unsigned int port_runtime_memory_bytes(void);

#endif
