#include <stdio.h>
#include <string.h>

#include "core/mock_backend.h"
#include "core/port_runtime.h"

static unsigned int tests_run;
static unsigned int tests_failed;
const unsigned char core_transaction_layout_bytes[sizeof(core_transaction_t)] = { 0 };
const unsigned char port_runtime_layout_bytes[sizeof(port_runtime_t)] = { 0 };

typedef struct completion_capture {
    unsigned int count;
    unsigned int id;
    unsigned int generation;
    unsigned int owner_id;
    core_transaction_status_t status;
    unsigned int metadata[CORE_TRANSPORT_METADATA_WORDS];
    core_operation_t operation;
    unsigned int ids[16];
} completion_capture_t;

static void ready_port(port_runtime_t *port, mock_backend_t *mock,
                       unsigned int index, core_tick_t now);
static void start_waiting(port_runtime_t *port, core_tick_t now);

#define CHECK(condition) do { \
    ++tests_run; \
    if (!(condition)) { \
        ++tests_failed; \
        printf("FAIL line=%d condition=%s\n", __LINE__, #condition); \
    } \
} while (0)

static void capture_completion(void *context,
                               const core_transaction_t *transaction,
                               core_transaction_status_t status,
                               const unsigned char *response,
                               unsigned int response_length)
{
    completion_capture_t *capture = (completion_capture_t *)context;
    ++capture->count;
    if (capture->count <= 16)
        capture->ids[capture->count - 1] = transaction->id;
    capture->id = transaction->id;
    capture->generation = transaction->generation;
    capture->owner_id = transaction->owner_id;
    capture->status = status;
    memcpy(capture->metadata, transaction->transport_metadata,
           sizeof(capture->metadata));
    capture->operation = transaction->operation;
    if (status == CORE_TX_COMPLETED)
        CHECK(response != 0 && response_length == 1);
}

static void test_operation_metadata_separation(void)
{
    port_runtime_t port; mock_backend_t mock; completion_capture_t capture;
    unsigned char payload=0x55; unsigned int metadata[4]={0xffff,0xf704,77,0xa5a5};
    core_operation_t operation={CORE_OPERATION_TX_ONLY,CORE_RESPONSE_NONE,0};
    memset(&capture,0,sizeof(capture)); ready_port(&port,&mock,0,0);
    port_runtime_set_completion(&port,capture_completion,&capture);
    CHECK(port_runtime_submit_operation(&port,&payload,1,9,3,metadata,&operation,
                                        1,10,10,0)==CORE_ACCEPTED);
    CHECK(port.queue.entries[0].operation.direction==CORE_OPERATION_TX_ONLY);
    CHECK(port.queue.entries[0].transport_metadata[1]==0xf704U);
    start_waiting(&port,1); port_runtime_step(&port,1);
    CHECK(capture.metadata[0]==0xffffU && capture.metadata[1]==0xf704U &&
          capture.metadata[2]==77U && capture.metadata[3]==0xa5a5U);
    CHECK(capture.operation.direction==CORE_OPERATION_TX_ONLY);
    CHECK(capture.operation.response_policy==CORE_RESPONSE_NONE);
}

static core_port_config_t config(unsigned int revision, unsigned int port)
{
    core_port_config_t result;
    result.revision = revision;
    result.mode = 0;
    result.baud = 9600;
    result.data_bits = 8;
    result.parity = 0;
    result.stop_bits = 1;
    result.special_baud_enabled = 0;
    result.special_baud = 0;
    result.transport = 0;
    result.endpoint_port = 502 + port;
    return result;
}

static void ready_port(port_runtime_t *port, mock_backend_t *mock,
                       unsigned int index, core_tick_t now)
{
    core_port_config_t initial = config(1, index);
    mock_backend_init(mock);
    mock->active_revision = initial.revision;
    port_runtime_init(port, index, &initial, mock_backend_ops(), mock);
    CHECK(port_runtime_start(port, now, 10) == 0);
    port_runtime_step(port, now);
    CHECK(port->state == PORT_READY);
}

static core_submit_result_t submit(port_runtime_t *port, core_tick_t now,
                                   core_tick_t queue_timeout,
                                   core_tick_t response_timeout)
{
    static const unsigned char request[] = { 1, 2, 3, 4 };
    return port_runtime_submit(port, request, sizeof(request), 7, 99, now,
                               queue_timeout, response_timeout, 0);
}

static void start_waiting(port_runtime_t *port, core_tick_t now)
{
    port_runtime_step(port, now);
    CHECK(port->state == PORT_ACTIVE);
    port_runtime_step(port, now);
    CHECK(port->state == PORT_WAITING);
}

static void test_normal_completion(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    completion_capture_t capture;
    memset(&capture, 0, sizeof(capture));
    ready_port(&port, &mock, 0, 0);
    port_runtime_set_completion(&port, capture_completion, &capture);
    CHECK(mock_backend_add_plan(&mock, MOCK_IMMEDIATE_SUCCESS, 0) == 0);
    CHECK(submit(&port, 1, 20, 10) == CORE_ACCEPTED);
    start_waiting(&port, 1);
    port_runtime_step(&port, 1);
    CHECK(port.state == PORT_READY);
    CHECK(port.stats.completed == 1);
    CHECK(port.stats.last_success_at == 1);
    CHECK(capture.count == 1);
    CHECK(capture.status == CORE_TX_COMPLETED);
    CHECK(capture.owner_id == 99);
    CHECK(capture.generation != 0);
}

static void test_timeout_and_recovery(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    ready_port(&port, &mock, 0, 0);
    CHECK(mock_backend_add_plan(&mock, MOCK_NO_RESPONSE, 0) == 0);
    CHECK(submit(&port, 1, 20, 3) == CORE_ACCEPTED);
    start_waiting(&port, 1);
    port_runtime_step(&port, 4);
    CHECK(port.state == PORT_RECOVERING);
    CHECK(port.stats.timeouts == 1);
    port_runtime_step(&port, 9);
    CHECK(port.state == PORT_READY);
    CHECK(port.stats.recoveries == 1);
}

static void test_fifo_order(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    completion_capture_t capture;
    unsigned int first;
    unsigned int second;
    memset(&capture, 0, sizeof(capture));
    ready_port(&port, &mock, 0, 0);
    port_runtime_set_completion(&port, capture_completion, &capture);
    CHECK(mock_backend_add_plan(&mock, MOCK_IMMEDIATE_SUCCESS, 0) == 0);
    CHECK(mock_backend_add_plan(&mock, MOCK_IMMEDIATE_SUCCESS, 0) == 0);
    {
        static const unsigned char request[] = { 1 };
        CHECK(port_runtime_submit(&port, request, 1, 0, 1, 0, 20, 10,
                                  &first) == CORE_ACCEPTED);
        CHECK(port_runtime_submit(&port, request, 1, 0, 2, 0, 20, 10,
                                  &second) == CORE_ACCEPTED);
    }
    port_runtime_step(&port, 0);
    port_runtime_step(&port, 0);
    port_runtime_step(&port, 0);
    port_runtime_step(&port, 1);
    port_runtime_step(&port, 1);
    port_runtime_step(&port, 1);
    CHECK(capture.count == 2);
    CHECK(capture.ids[0] == first);
    CHECK(capture.ids[1] == second);
}

static void test_open_failure_bounded(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    core_port_config_t initial = config(1, 0);
    mock_backend_init(&mock);
    mock.open_failures_remaining = 100;
    port_runtime_init(&port, 0, &initial, mock_backend_ops(), &mock);
    CHECK(port_runtime_start(&port, 0, 2) == 0);
    port_runtime_step(&port, 0);
    CHECK(port.state == PORT_STARTING);
    port_runtime_step(&port, 2);
    CHECK(port.state == PORT_RECOVERING);
    port_runtime_step(&port, 7);
    CHECK(port.state == PORT_READY);
    CHECK(port.stats.recoveries == 1);
}

static void test_late_response_barrier(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    completion_capture_t capture;
    memset(&capture, 0, sizeof(capture));
    ready_port(&port, &mock, 0, 0);
    port_runtime_set_completion(&port, capture_completion, &capture);
    CHECK(mock_backend_add_plan(&mock, MOCK_LATE_RESPONSE, 10) == 0);
    CHECK(mock_backend_add_plan(&mock, MOCK_DELAYED_SUCCESS, 4) == 0);
    CHECK(submit(&port, 0, 20, 3) == CORE_ACCEPTED);
    start_waiting(&port, 0);
    port_runtime_step(&port, 3);
    port_runtime_step(&port, 8);
    CHECK(port.state == PORT_READY);
    CHECK(submit(&port, 8, 20, 10) == CORE_ACCEPTED);
    start_waiting(&port, 8);
    port_runtime_step(&port, 10);
    CHECK(port.state == PORT_WAITING);
    CHECK(port.stats.stale_responses == 1);
    port_runtime_step(&port, 12);
    CHECK(port.state == PORT_READY);
    CHECK(port.stats.completed == 1);
    CHECK(capture.count == 2);
    CHECK(capture.status == CORE_TX_COMPLETED);
}

static void test_queue_bounds(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    unsigned char payload[128];
    unsigned int index;
    unsigned int memory_before;
    memset(payload, 0x5a, sizeof(payload));
    ready_port(&port, &mock, 0, 0);
    memory_before = port_runtime_memory_bytes();
    for (index = 0; index < CORE_QUEUE_CAPACITY; ++index)
        CHECK(port_runtime_submit(&port, payload, sizeof(payload), 0, index, 0,
                                  10, 10, 0) == CORE_ACCEPTED);
    for (index = 0; index < 1000; ++index)
        CHECK(port_runtime_submit(&port, payload, sizeof(payload), 0, index, 0,
                                  10, 10, 0) == CORE_QUEUE_FULL);
    CHECK(port.queue.count == CORE_QUEUE_CAPACITY);
    CHECK(port.queue.bytes == CORE_QUEUE_BYTES_MAX);
    CHECK(port.stats.queue_high_water == CORE_QUEUE_CAPACITY);
    CHECK(port_runtime_memory_bytes() == memory_before);
}

static void test_cancel_ownership(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    ready_port(&port, &mock, 0, 0);
    CHECK(mock_backend_add_plan(&mock, MOCK_DELAYED_SUCCESS, 20) == 0);
    CHECK(submit(&port, 0, 20, 30) == CORE_ACCEPTED);
    start_waiting(&port, 0);
    CHECK(port_runtime_cancel_active(&port, 1) == 0);
    CHECK(!port.has_active);
    CHECK(port.state == PORT_RECOVERING);
    CHECK(port.stats.cancelled == 1);
    CHECK(mock.last_cancelled_generation != 0);
}

static void test_backend_failure(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    ready_port(&port, &mock, 0, 0);
    CHECK(mock_backend_add_plan(&mock, MOCK_ACTIVE_FAILURE, 1) == 0);
    CHECK(submit(&port, 0, 20, 10) == CORE_ACCEPTED);
    start_waiting(&port, 0);
    port_runtime_step(&port, 1);
    CHECK(port.state == PORT_RECOVERING);
    port_runtime_step(&port, 6);
    CHECK(port.state == PORT_READY);
}

static void test_malformed_response(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    ready_port(&port, &mock, 0, 0);
    CHECK(mock_backend_add_plan(&mock, MOCK_MALFORMED_RESPONSE, 0) == 0);
    CHECK(submit(&port, 0, 20, 10) == CORE_ACCEPTED);
    start_waiting(&port, 0);
    port_runtime_step(&port, 0);
    CHECK(port.state == PORT_RECOVERING);
    CHECK(port.stats.invalid == 1);
}

static void test_bounded_recovery(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    ready_port(&port, &mock, 0, 0);
    mock.recovery_failures_remaining = 3;
    CHECK(mock_backend_add_plan(&mock, MOCK_NO_RESPONSE, 0) == 0);
    CHECK(submit(&port, 0, 20, 1) == CORE_ACCEPTED);
    start_waiting(&port, 0);
    port_runtime_step(&port, 1);
    port_runtime_step(&port, 6);
    port_runtime_step(&port, 11);
    port_runtime_step(&port, 16);
    CHECK(port.state == PORT_DEGRADED);
    CHECK(port.stats.recovery_failures == 3);
}

static void test_reconfiguration(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    core_port_config_t changed;
    ready_port(&port, &mock, 0, 0);
    changed = config(2, 0);
    changed.baud = 19200;
    mock.reconfigure_failures_remaining = 1;
    CHECK(port_runtime_request_reconfigure(&port, &changed, 1, 10) == 0);
    CHECK(submit(&port, 1, 10, 10) == CORE_RECONFIGURING);
    port_runtime_step(&port, 1);
    CHECK(port.state == PORT_READY);
    CHECK(port.current_config.revision == 1);
    CHECK(port.stats.rollbacks == 1);

    CHECK(port_runtime_request_reconfigure(&port, &changed, 2, 10) == 0);
    port_runtime_step(&port, 2);
    CHECK(port.current_config.revision == 2);
    CHECK(port.current_config.baud == 19200);
    CHECK(port.stats.reconfigurations == 1);
}

static void test_cross_port_isolation(void)
{
    port_runtime_t ports[CORE_PORT_COUNT];
    mock_backend_t mocks[CORE_PORT_COUNT];
    unsigned int index;
    core_tick_t now;
    for (index = 0; index < CORE_PORT_COUNT; ++index)
        ready_port(&ports[index], &mocks[index], index, 0);
    CHECK(mock_backend_add_plan(&mocks[0], MOCK_NO_RESPONSE, 0) == 0);
    for (index = 1; index < CORE_PORT_COUNT; ++index)
        CHECK(mock_backend_add_plan(&mocks[index], MOCK_IMMEDIATE_SUCCESS, 0) == 0);
    for (index = 0; index < CORE_PORT_COUNT; ++index)
        CHECK(submit(&ports[index], 0, 20, 10) == CORE_ACCEPTED);
    for (now = 0; now < 4; ++now) {
        for (index = 0; index < CORE_PORT_COUNT; ++index)
            port_runtime_step(&ports[index], now);
    }
    CHECK(ports[0].state == PORT_WAITING);
    for (index = 1; index < CORE_PORT_COUNT; ++index)
        CHECK(ports[index].stats.completed == 1);
}

static void test_reconfigure_one_port_only(void)
{
    port_runtime_t ports[CORE_PORT_COUNT];
    mock_backend_t mocks[CORE_PORT_COUNT];
    core_port_config_t changed;
    unsigned int index;
    for (index = 0; index < CORE_PORT_COUNT; ++index)
        ready_port(&ports[index], &mocks[index], index, 0);
    changed = ports[0].current_config;
    changed.revision = 2;
    changed.baud = 38400;
    CHECK(port_runtime_request_reconfigure(&ports[0], &changed, 1, 5) == 0);
    port_runtime_step(&ports[0], 1);
    CHECK(ports[0].current_config.baud == 38400);
    for (index = 1; index < CORE_PORT_COUNT; ++index) {
        CHECK(ports[index].state == PORT_READY);
        CHECK(ports[index].current_config.revision == 1);
        CHECK(ports[index].current_config.baud == 9600);
    }
}

static void test_repeated_cycles(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    unsigned int cycle;
    core_tick_t now = 0;
    ready_port(&port, &mock, 0, now);
    for (cycle = 0; cycle < 2000; ++cycle) {
        mock.plan_count = 1;
        mock.plan_next = 0;
        mock.plans[0].behavior = MOCK_NO_RESPONSE;
        mock.plans[0].delay = 0;
        CHECK(submit(&port, now, 20, 1) == CORE_ACCEPTED);
        port_runtime_step(&port, now);
        port_runtime_step(&port, now);
        ++now;
        port_runtime_step(&port, now);
        now += port.recovery_delay;
        port_runtime_step(&port, now);
        CHECK(port.state == PORT_READY);
        CHECK(port.queue.count == 0);
        CHECK(!port.has_active);
    }
    CHECK(port.stats.timeouts == 2000);
    CHECK(port.stats.recoveries == 2000);
}

static void test_deadline_wrap_and_wall_clock_irrelevance(void)
{
    core_deadline_t deadline = core_deadline_after(0xfffffff0U, 32);
    long fake_wall_clock = 2000000000L;
    CHECK(!core_deadline_expired(deadline, 0xffffffffU));
    fake_wall_clock = -2000000000L;
    CHECK(!core_deadline_expired(deadline, 0x0000000fU));
    CHECK(core_deadline_expired(deadline, 0x00000010U));
    CHECK(fake_wall_clock == -2000000000L);
    CHECK(!core_ticks_valid_interval(0x80000000U));
}

static void test_graceful_stop(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    ready_port(&port, &mock, 0, 0);
    CHECK(submit(&port, 0, 20, 20) == CORE_ACCEPTED);
    CHECK(port_runtime_request_stop(&port, 1, 5) == 0);
    CHECK(port.queue.count == 0);
    port_runtime_step(&port, 1);
    CHECK(port.state == PORT_DISABLED);

    ready_port(&port, &mock, 0, 10);
    mock.stop_failures_remaining = 100;
    CHECK(port_runtime_request_stop(&port, 10, 3) == 0);
    port_runtime_step(&port, 10);
    port_runtime_step(&port, 13);
    CHECK(port.state == PORT_ERROR);
}

static void test_start_stop_reconfigure_soak(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    core_port_config_t changed;
    unsigned int cycle;
    core_tick_t now = 0;
    core_port_config_t initial = config(1, 0);
    mock_backend_init(&mock);
    mock.active_revision = 1;
    port_runtime_init(&port, 0, &initial, mock_backend_ops(), &mock);
    for (cycle = 0; cycle < 1000; ++cycle) {
        CHECK(port_runtime_start(&port, now, 5) == 0);
        port_runtime_step(&port, now);
        CHECK(port.state == PORT_READY);
        changed = port.current_config;
        ++changed.revision;
        changed.baud = changed.baud == 9600 ? 19200 : 9600;
        CHECK(port_runtime_request_reconfigure(&port, &changed, now, 5) == 0);
        port_runtime_step(&port, now);
        CHECK(port.state == PORT_READY);
        CHECK(port_runtime_request_stop(&port, now, 5) == 0);
        port_runtime_step(&port, now);
        CHECK(port.state == PORT_DISABLED);
        ++now;
    }
    CHECK(port.queue.count == 0);
    CHECK(!port.has_active);
}

static void test_invalid_and_illegal(void)
{
    port_runtime_t port;
    mock_backend_t mock;
    unsigned char byte = 1;
    core_port_config_t invalid = config(2, 0);
    ready_port(&port, &mock, 0, 0);
    CHECK(port_runtime_transition(&port, PORT_DISABLED) < 0);
    CHECK(port.stats.illegal_transitions == 1);
    CHECK(port_runtime_submit(&port, &byte, 0, 0, 0, 0, 1, 1, 0) ==
          CORE_INVALID_REQUEST);
    invalid.endpoint_port = 0;
    CHECK(port_runtime_request_reconfigure(&port, &invalid, 0, 1) < 0);
}

int main(void)
{
    test_normal_completion();
    test_operation_metadata_separation();
    test_timeout_and_recovery();
    test_fifo_order();
    test_open_failure_bounded();
    test_late_response_barrier();
    test_queue_bounds();
    test_cancel_ownership();
    test_backend_failure();
    test_malformed_response();
    test_bounded_recovery();
    test_reconfiguration();
    test_cross_port_isolation();
    test_reconfigure_one_port_only();
    test_repeated_cycles();
    test_deadline_wrap_and_wall_clock_irrelevance();
    test_graceful_stop();
    test_start_stop_reconfigure_soak();
    test_invalid_and_illegal();
    printf("transport_core_tests checks=%u failed=%u runtime_bytes=%u tx_bytes=%u\n",
           tests_run, tests_failed, port_runtime_memory_bytes(),
           (unsigned int)sizeof(core_transaction_t));
    return tests_failed == 0 ? 0 : 1;
}
