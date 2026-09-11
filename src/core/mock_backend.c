#include <string.h>

#include "core/mock_backend.h"

static int mock_open(void *context, core_tick_t now)
{
    mock_backend_t *mock = (mock_backend_t *)context;
    (void)now;
    ++mock->opens;
    if (mock->open_failures_remaining > 0) {
        --mock->open_failures_remaining;
        return -1;
    }
    return 0;
}

static int schedule_event(mock_backend_t *mock, backend_event_type_t type,
                          unsigned int generation, core_tick_t now,
                          core_tick_t delay)
{
    unsigned int index;
    for (index = 0; index < MOCK_EVENT_CAPACITY; ++index) {
        if (!mock->events[index].used) {
            mock->events[index].used = 1;
            memset(&mock->events[index].event, 0,
                   sizeof(mock->events[index].event));
            mock->events[index].event.type = type;
            mock->events[index].event.generation = generation;
            mock->events[index].event.length = 1;
            mock->events[index].event.data[0] = 0x55;
            mock->events[index].due = core_deadline_after(now, delay);
            return 0;
        }
    }
    return -1;
}

static int mock_start(void *context, const core_transaction_t *transaction,
                      core_tick_t now)
{
    mock_backend_t *mock = (mock_backend_t *)context;
    mock_plan_t plan;
    ++mock->starts;
    plan.behavior = MOCK_IMMEDIATE_SUCCESS;
    plan.delay = 0;
    if (mock->plan_next < mock->plan_count)
        plan = mock->plans[mock->plan_next++];
    if (plan.behavior == MOCK_START_FAILURE)
        return -1;
    if (plan.behavior == MOCK_NO_RESPONSE)
        return 0;
    if (plan.behavior == MOCK_MALFORMED_RESPONSE)
        return schedule_event(mock, BACKEND_EVENT_MALFORMED,
                              transaction->generation, now, plan.delay);
    if (plan.behavior == MOCK_ACTIVE_FAILURE)
        return schedule_event(mock, BACKEND_EVENT_FAILURE,
                              transaction->generation, now, plan.delay);
    return schedule_event(mock, BACKEND_EVENT_RESPONSE,
                          transaction->generation, now, plan.delay);
}

static int mock_poll(void *context, core_tick_t now, backend_event_t *event)
{
    mock_backend_t *mock = (mock_backend_t *)context;
    unsigned int index;
    for (index = 0; index < MOCK_EVENT_CAPACITY; ++index) {
        if (mock->events[index].used &&
            core_deadline_expired(mock->events[index].due, now)) {
            *event = mock->events[index].event;
            mock->events[index].used = 0;
            return 1;
        }
    }
    event->type = BACKEND_EVENT_NONE;
    return 0;
}

static void mock_cancel(void *context, unsigned int generation)
{
    mock_backend_t *mock = (mock_backend_t *)context;
    ++mock->cancels;
    mock->last_cancelled_generation = generation;
}

static int mock_recover(void *context, core_tick_t now)
{
    mock_backend_t *mock = (mock_backend_t *)context;
    (void)now;
    ++mock->recovers;
    if (mock->recovery_failures_remaining > 0) {
        --mock->recovery_failures_remaining;
        return -1;
    }
    return 0;
}

static int mock_reconfigure(void *context, const core_port_config_t *config,
                            core_tick_t now)
{
    mock_backend_t *mock = (mock_backend_t *)context;
    (void)now;
    ++mock->reconfigures;
    if (config->revision < mock->active_revision) {
        if (mock->rollback_failures_remaining > 0) {
            --mock->rollback_failures_remaining;
            return -1;
        }
    } else if (mock->reconfigure_failures_remaining > 0) {
        --mock->reconfigure_failures_remaining;
        return -1;
    }
    mock->active_revision = config->revision;
    return 0;
}

static int mock_stop(void *context, core_tick_t now)
{
    mock_backend_t *mock = (mock_backend_t *)context;
    (void)now;
    ++mock->stops;
    if (mock->stop_failures_remaining > 0) {
        --mock->stop_failures_remaining;
        return -1;
    }
    return 0;
}

static const core_backend_ops_t operations = {
    mock_open, mock_start, mock_poll, mock_cancel,
    mock_recover, mock_reconfigure, mock_stop
};

void mock_backend_init(mock_backend_t *mock)
{
    memset(mock, 0, sizeof(*mock));
}

int mock_backend_add_plan(mock_backend_t *mock, mock_behavior_t behavior,
                          core_tick_t delay)
{
    if (mock->plan_count >= MOCK_PLAN_CAPACITY ||
        !core_ticks_valid_interval(delay))
        return -1;
    mock->plans[mock->plan_count].behavior = behavior;
    mock->plans[mock->plan_count].delay = delay;
    ++mock->plan_count;
    return 0;
}

const core_backend_ops_t *mock_backend_ops(void)
{
    return &operations;
}

unsigned int mock_backend_pending_events(const mock_backend_t *mock)
{
    unsigned int index;
    unsigned int count = 0;
    for (index = 0; index < MOCK_EVENT_CAPACITY; ++index) {
        if (mock->events[index].used)
            ++count;
    }
    return count;
}
