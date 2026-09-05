#include <string.h>

#include "core/port_runtime.h"

static void increment(unsigned int *value)
{
    if (*value != 0xffffffffU)
        ++*value;
}

static void set_error(port_runtime_t *port, const char *message)
{
    strncpy(port->last_error, message, CORE_ERROR_LENGTH - 1);
    port->last_error[CORE_ERROR_LENGTH - 1] = '\0';
}

static int config_valid(const core_port_config_t *config)
{
    return config != 0 && config->mode <= 3 && config->baud > 0 &&
           config->data_bits >= 5 && config->data_bits <= 8 &&
           config->parity <= 2 &&
           (config->stop_bits == 1 || config->stop_bits == 2) &&
           config->endpoint_port >= 1 && config->endpoint_port <= 65535;
}

static int backend_valid(const core_backend_ops_t *backend)
{
    return backend != 0 && backend->open != 0 && backend->start != 0 &&
           backend->poll != 0 && backend->cancel != 0 &&
           backend->recover != 0 && backend->reconfigure != 0 &&
           backend->stop != 0;
}

static int transition_allowed(port_state_t from, port_state_t to)
{
    switch (from) {
    case PORT_DISABLED:
        return to == PORT_STARTING;
    case PORT_STARTING:
        return to == PORT_READY || to == PORT_RECOVERING ||
               to == PORT_STOPPING || to == PORT_ERROR;
    case PORT_READY:
        return to == PORT_ACTIVE || to == PORT_RECOVERING ||
               to == PORT_RECONFIGURING || to == PORT_STOPPING;
    case PORT_ACTIVE:
        return to == PORT_WAITING || to == PORT_RECOVERING ||
               to == PORT_RECONFIGURING || to == PORT_STOPPING;
    case PORT_WAITING:
        return to == PORT_READY || to == PORT_RECOVERING ||
               to == PORT_RECONFIGURING || to == PORT_STOPPING;
    case PORT_RECOVERING:
        return to == PORT_READY || to == PORT_DEGRADED ||
               to == PORT_RECONFIGURING || to == PORT_STOPPING ||
               to == PORT_ERROR;
    case PORT_DEGRADED:
        return to == PORT_RECOVERING || to == PORT_RECONFIGURING ||
               to == PORT_STOPPING || to == PORT_ERROR;
    case PORT_RECONFIGURING:
        return to == PORT_READY || to == PORT_STOPPING || to == PORT_ERROR;
    case PORT_STOPPING:
        return to == PORT_DISABLED || to == PORT_ERROR;
    case PORT_ERROR:
        return to == PORT_STARTING || to == PORT_STOPPING ||
               to == PORT_DISABLED;
    default:
        return 0;
    }
}

int port_runtime_transition(port_runtime_t *port, port_state_t next)
{
    int next_index = (int)next;
    if (port == 0 || next_index < 0 || next_index >= PORT_STATE_COUNT ||
        !transition_allowed(port->state, next)) {
        if (port != 0) {
            increment(&port->stats.illegal_transitions);
            set_error(port, "illegal transition");
        }
        return -1;
    }
    port->state = next;
    return 0;
}

void port_runtime_init(port_runtime_t *port, unsigned int port_index,
                       const core_port_config_t *config,
                       const core_backend_ops_t *backend, void *backend_context)
{
    memset(port, 0, sizeof(*port));
    port->port_index = port_index;
    port->state = PORT_DISABLED;
    port->current_config = *config;
    port->known_good_config = *config;
    port->next_transaction_id = 1;
    port->recovery_delay = 5;
    port->backend = backend;
    port->backend_context = backend_context;
}

void port_runtime_set_completion(port_runtime_t *port,
                                 core_completion_fn completion,
                                 void *completion_context)
{
    port->completion = completion;
    port->completion_context = completion_context;
}

static void notify_completion(port_runtime_t *port,
                              const core_transaction_t *transaction,
                              core_transaction_status_t status,
                              const unsigned char *response,
                              unsigned int response_length)
{
    if (port->completion != 0)
        port->completion(port->completion_context, transaction, status,
                         response, response_length);
}

int port_runtime_start(port_runtime_t *port, core_tick_t now,
                       core_tick_t start_timeout)
{
    if (port == 0 || !backend_valid(port->backend) ||
        !core_ticks_valid_interval(start_timeout))
        return -1;
    if (port_runtime_transition(port, PORT_STARTING) < 0)
        return -1;
    port->state_deadline = core_deadline_after(now, start_timeout);
    return 0;
}

static int queue_push(transaction_queue_t *queue,
                      const core_transaction_t *transaction)
{
    unsigned int tail;
    if (queue->count >= CORE_QUEUE_CAPACITY ||
        queue->bytes + transaction->payload_length > CORE_QUEUE_BYTES_MAX)
        return -1;
    tail = (queue->head + queue->count) % CORE_QUEUE_CAPACITY;
    queue->entries[tail] = *transaction;
    ++queue->count;
    queue->bytes += transaction->payload_length;
    return 0;
}

static int queue_pop(transaction_queue_t *queue, core_transaction_t *transaction)
{
    if (queue->count == 0)
        return -1;
    *transaction = queue->entries[queue->head];
    queue->bytes -= transaction->payload_length;
    queue->head = (queue->head + 1) % CORE_QUEUE_CAPACITY;
    --queue->count;
    return 0;
}

static void update_depth(port_runtime_t *port)
{
    port->stats.queue_depth = port->queue.count;
    if (port->stats.queue_depth > port->stats.queue_high_water)
        port->stats.queue_high_water = port->stats.queue_depth;
}

core_submit_result_t port_runtime_submit(port_runtime_t *port,
                                         const unsigned char *payload,
                                         unsigned int length,
                                         unsigned int owner_kind,
                                         unsigned int owner_id,
                                         core_tick_t now,
                                         core_tick_t queue_timeout,
                                         core_tick_t response_timeout,
                                         unsigned int *transaction_id)
{
    return port_runtime_submit_with_metadata(
        port, payload, length, owner_kind, owner_id, 0, now,
        queue_timeout, response_timeout, transaction_id);
}

core_submit_result_t port_runtime_submit_with_metadata(
    port_runtime_t *port, const unsigned char *payload, unsigned int length,
    unsigned int owner_kind, unsigned int owner_id,
    const unsigned int metadata[CORE_TRANSPORT_METADATA_WORDS],
    core_tick_t now, core_tick_t queue_timeout, core_tick_t response_timeout,
    unsigned int *transaction_id)
{
    core_operation_t operation;
    operation.direction = CORE_OPERATION_TX_THEN_RX;
    operation.response_policy = CORE_RESPONSE_BACKEND_FRAMED;
    operation.expected_response_length = 0;
    return port_runtime_submit_operation(port, payload, length, owner_kind,
        owner_id, metadata, &operation, now, queue_timeout, response_timeout,
        transaction_id);
}

static int operation_valid(const core_operation_t *operation)
{
    if (operation == 0 || operation->direction > CORE_OPERATION_TX_THEN_RX ||
        operation->response_policy > CORE_RESPONSE_BACKEND_FRAMED)
        return 0;
    if (operation->direction == CORE_OPERATION_TX_ONLY)
        return operation->response_policy == CORE_RESPONSE_NONE &&
               operation->expected_response_length == 0;
    if (operation->response_policy == CORE_RESPONSE_FIXED_LENGTH)
        return operation->expected_response_length > 0 &&
               operation->expected_response_length <= CORE_TRANSACTION_PAYLOAD_MAX;
    return operation->response_policy == CORE_RESPONSE_BACKEND_FRAMED &&
           operation->expected_response_length == 0;
}

core_submit_result_t port_runtime_submit_operation(
    port_runtime_t *port, const unsigned char *payload, unsigned int length,
    unsigned int owner_kind, unsigned int owner_id,
    const unsigned int metadata[CORE_TRANSPORT_METADATA_WORDS],
    const core_operation_t *operation,
    core_tick_t now, core_tick_t queue_timeout, core_tick_t response_timeout,
    unsigned int *transaction_id)
{
    core_transaction_t transaction;
    unsigned int index;
    if (port == 0 || payload == 0 || length == 0 ||
        length > CORE_TRANSACTION_PAYLOAD_MAX ||
        !core_ticks_valid_interval(queue_timeout) || queue_timeout == 0 ||
        !core_ticks_valid_interval(response_timeout) || response_timeout == 0 ||
        !operation_valid(operation)) {
        if (port != 0)
            increment(&port->stats.invalid);
        return CORE_INVALID_REQUEST;
    }
    if (port->state == PORT_DISABLED || port->state == PORT_STOPPING ||
        port->state == PORT_ERROR)
        return CORE_PORT_DISABLED;
    if (port->state == PORT_RECOVERING || port->state == PORT_DEGRADED ||
        port->state == PORT_STARTING)
        return CORE_PORT_RECOVERING;
    if (port->state == PORT_RECONFIGURING)
        return CORE_RECONFIGURING;

    memset(&transaction, 0, sizeof(transaction));
    transaction.id = port->next_transaction_id++;
    if (port->next_transaction_id == 0)
        port->next_transaction_id = 1;
    transaction.owner_kind = owner_kind;
    transaction.owner_id = owner_id;
    if (metadata != 0)
        for (index = 0; index < CORE_TRANSPORT_METADATA_WORDS; ++index)
            transaction.transport_metadata[index] = metadata[index];
    transaction.operation = *operation;
    transaction.enqueued_at = now;
    transaction.queue_deadline = core_deadline_after(now, queue_timeout);
    transaction.response_timeout = response_timeout;
    transaction.payload_length = length;
    memcpy(transaction.payload, payload, length);
    transaction.status = CORE_TX_QUEUED;
    if (queue_push(&port->queue, &transaction) < 0) {
        increment(&port->stats.queue_full);
        return CORE_QUEUE_FULL;
    }
    increment(&port->stats.accepted);
    update_depth(port);
    if (transaction_id != 0)
        *transaction_id = transaction.id;
    return CORE_ACCEPTED;
}

static void clear_active(port_runtime_t *port)
{
    memset(&port->active, 0, sizeof(port->active));
    port->has_active = 0;
}

static void begin_recovery(port_runtime_t *port, core_tick_t now,
                           const char *error)
{
    if (port->has_active) {
        port->response_barrier_generation = port->active.generation;
        port->backend->cancel(port->backend_context, port->active.generation);
        clear_active(port);
    }
    set_error(port, error);
    port->recovery_attempts = 0;
    port->state_deadline = core_deadline_after(now, port->recovery_delay);
    port_runtime_transition(port, PORT_RECOVERING);
}

int port_runtime_report_io_failure(port_runtime_t *port,core_tick_t now,const char *message)
{
    if(!port||!message||port->state!=PORT_READY||port->has_active||port->queue.count)return -1;
    increment(&port->stats.backend_failures);
    begin_recovery(port,now,message);
    return 0;
}

static void flush_queue(port_runtime_t *port)
{
    core_transaction_t transaction;
    while (queue_pop(&port->queue, &transaction) == 0) {
        transaction.status = CORE_TX_CANCELLED;
        transaction.cancelled = 1;
        notify_completion(port, &transaction, CORE_TX_CANCELLED, 0, 0);
        increment(&port->stats.cancelled);
    }
    update_depth(port);
}

static void step_waiting(port_runtime_t *port, core_tick_t now)
{
    backend_event_t event;
    int result;
    memset(&event, 0, sizeof(event));
    result = port->backend->poll(port->backend_context, now, &event);
    if (result > 0 && event.type != BACKEND_EVENT_NONE) {
        if (!port->has_active || event.generation != port->active.generation) {
            increment(&port->stats.stale_responses);
            return;
        }
        if (event.type == BACKEND_EVENT_TRANSMITTED) {
            port->active.request_transmitted = 1;
            return;
        }
        if (event.type == BACKEND_EVENT_RESPONSE) {
            port->active.request_transmitted = 1;
            port->active.status = CORE_TX_COMPLETED;
            increment(&port->stats.completed);
            port->stats.last_success_at = now;
            notify_completion(port, &port->active, CORE_TX_COMPLETED,
                              event.data, event.length);
            clear_active(port);
            port_runtime_transition(port, PORT_READY);
            return;
        }
        if (event.type == BACKEND_EVENT_MALFORMED) {
            port->active.request_transmitted = 1;
            port->active.status = CORE_TX_MALFORMED;
            increment(&port->stats.invalid);
            notify_completion(port, &port->active, CORE_TX_MALFORMED,
                              event.data, event.length);
            begin_recovery(port, now, "malformed response");
            return;
        }
        increment(&port->stats.backend_failures);
        port->active.status = CORE_TX_FAILED;
        notify_completion(port, &port->active, CORE_TX_FAILED, 0, 0);
        begin_recovery(port, now, "backend failure");
        return;
    }
    if (port->has_active &&
        core_deadline_expired(port->active.response_deadline, now)) {
        port->active.status = CORE_TX_TIMED_OUT;
        increment(&port->stats.timeouts);
        notify_completion(port, &port->active, CORE_TX_TIMED_OUT, 0, 0);
        begin_recovery(port, now, "response timeout");
    }
}

void port_runtime_step(port_runtime_t *port, core_tick_t now)
{
    backend_event_t event;
    int result;
    if (port == 0 || !backend_valid(port->backend))
        return;
    switch (port->state) {
    case PORT_STARTING:
        result = port->backend->open(port->backend_context, now);
        if (result == 0) {
            port->recovery_attempts = 0;
            port_runtime_transition(port, PORT_READY);
        } else if (core_deadline_expired(port->state_deadline, now)) {
            begin_recovery(port, now, "backend open failed");
        }
        break;
    case PORT_READY:
        while (queue_pop(&port->queue, &port->active) == 0) {
            update_depth(port);
            if (core_deadline_expired(port->active.queue_deadline, now)) {
                increment(&port->stats.timeouts);
                port->active.status = CORE_TX_TIMED_OUT;
                notify_completion(port, &port->active, CORE_TX_TIMED_OUT, 0, 0);
                clear_active(port);
                continue;
            }
            port->has_active = 1;
            ++port->generation;
            if (port->generation == 0)
                ++port->generation;
            port->active.generation = port->generation;
            port->active.started_at = now;
            port->active.response_deadline =
                core_deadline_after(now, port->active.response_timeout);
            port->active.status = CORE_TX_ACTIVE;
            port_runtime_transition(port, PORT_ACTIVE);
            break;
        }
        break;
    case PORT_ACTIVE:
        result = port->backend->start(port->backend_context, &port->active, now);
        if (result == 0) {
            port_runtime_transition(port, PORT_WAITING);
        } else {
            increment(&port->stats.backend_failures);
            port->active.status = CORE_TX_FAILED;
            notify_completion(port, &port->active, CORE_TX_FAILED, 0, 0);
            begin_recovery(port, now, "backend start failed");
        }
        break;
    case PORT_WAITING:
        step_waiting(port, now);
        break;
    case PORT_RECOVERING:
        memset(&event, 0, sizeof(event));
        result = port->backend->poll(port->backend_context, now, &event);
        if (result > 0 && event.type != BACKEND_EVENT_NONE)
            increment(&port->stats.stale_responses);
        if (core_deadline_expired(port->state_deadline, now)) {
            result = port->backend->recover(port->backend_context, now);
            if (result == 0) {
                increment(&port->stats.recoveries);
                port->recovery_attempts = 0;
                port_runtime_transition(port, PORT_READY);
            } else {
                increment(&port->stats.recovery_failures);
                ++port->recovery_attempts;
                if (port->recovery_attempts >= CORE_RECOVERY_MAX_ATTEMPTS)
                    port_runtime_transition(port, PORT_DEGRADED);
                else
                    port->state_deadline =
                        core_deadline_after(now, port->recovery_delay);
            }
        }
        break;
    case PORT_RECONFIGURING:
        if (core_deadline_expired(port->state_deadline, now)) {
            set_error(port, "reconfigure timeout");
            if (port->backend->reconfigure(port->backend_context,
                                           &port->known_good_config, now) == 0) {
                increment(&port->stats.rollbacks);
                port_runtime_transition(port, PORT_READY);
            } else {
                port_runtime_transition(port, PORT_ERROR);
            }
        } else if (port->backend->reconfigure(port->backend_context,
                                              &port->staged_config, now) == 0) {
            port->current_config = port->staged_config;
            port->known_good_config = port->staged_config;
            increment(&port->stats.reconfigurations);
            port_runtime_transition(port, PORT_READY);
        } else if (port->backend->reconfigure(port->backend_context,
                                              &port->known_good_config, now) == 0) {
            set_error(port, "reconfigure rolled back");
            increment(&port->stats.rollbacks);
            port_runtime_transition(port, PORT_READY);
        } else {
            set_error(port, "rollback failed");
            port_runtime_transition(port, PORT_ERROR);
        }
        break;
    case PORT_STOPPING:
        result = port->backend->stop(port->backend_context, now);
        if (result == 0)
            port_runtime_transition(port, PORT_DISABLED);
        else if (core_deadline_expired(port->state_deadline, now)) {
            set_error(port, "stop timeout");
            port_runtime_transition(port, PORT_ERROR);
        }
        break;
    default:
        break;
    }
}

int port_runtime_cancel_active(port_runtime_t *port, core_tick_t now)
{
    if (port == 0 || !port->has_active)
        return -1;
    port->active.cancelled = 1;
    port->active.status = CORE_TX_CANCELLED;
    port->response_barrier_generation = port->active.generation;
    port->backend->cancel(port->backend_context, port->active.generation);
    increment(&port->stats.cancelled);
    notify_completion(port, &port->active, CORE_TX_CANCELLED, 0, 0);
    clear_active(port);
    port->recovery_attempts = 0;
    port->state_deadline = core_deadline_after(now, port->recovery_delay);
    return port_runtime_transition(port, PORT_RECOVERING);
}

int port_runtime_detach_listener(port_runtime_t *port, core_tick_t now)
{
    if (!port) return -1;
    flush_queue(port);
    return port->has_active ? port_runtime_cancel_active(port,now) : 0;
}

int port_runtime_request_reconfigure(port_runtime_t *port,
                                     const core_port_config_t *config,
                                     core_tick_t now,
                                     core_tick_t timeout)
{
    if (port == 0 || !config_valid(config) || timeout == 0 ||
        !core_ticks_valid_interval(timeout))
        return -1;
    if (port->state != PORT_READY && port->state != PORT_ACTIVE &&
        port->state != PORT_WAITING && port->state != PORT_RECOVERING &&
        port->state != PORT_DEGRADED)
        return -1;
    if (port->has_active) {
        port->backend->cancel(port->backend_context, port->active.generation);
        increment(&port->stats.cancelled);
        port->active.status = CORE_TX_CANCELLED;
        notify_completion(port, &port->active, CORE_TX_CANCELLED, 0, 0);
        clear_active(port);
    }
    flush_queue(port);
    port->known_good_config = port->current_config;
    port->staged_config = *config;
    port->state_deadline = core_deadline_after(now, timeout);
    return port_runtime_transition(port, PORT_RECONFIGURING);
}

int port_runtime_request_stop(port_runtime_t *port, core_tick_t now,
                              core_tick_t timeout)
{
    if (port == 0 || timeout == 0 || !core_ticks_valid_interval(timeout) ||
        port->state == PORT_DISABLED || port->state == PORT_STOPPING)
        return -1;
    if (port->has_active) {
        port->backend->cancel(port->backend_context, port->active.generation);
        increment(&port->stats.cancelled);
        port->active.status = CORE_TX_CANCELLED;
        notify_completion(port, &port->active, CORE_TX_CANCELLED, 0, 0);
        clear_active(port);
    }
    flush_queue(port);
    port->state_deadline = core_deadline_after(now, timeout);
    return port_runtime_transition(port, PORT_STOPPING);
}

core_tick_t port_runtime_active_age(const port_runtime_t *port, core_tick_t now)
{
    return port != 0 && port->has_active ?
           core_elapsed(port->active.started_at, now) : 0;
}

unsigned int port_runtime_memory_bytes(void)
{
    return (unsigned int)sizeof(port_runtime_t);
}
