#ifndef FOURVRS_CORE_MOCK_BACKEND_H
#define FOURVRS_CORE_MOCK_BACKEND_H

#include "core/port_runtime.h"

#define MOCK_PLAN_CAPACITY 32
#define MOCK_EVENT_CAPACITY 16

typedef enum mock_behavior {
    MOCK_IMMEDIATE_SUCCESS = 0,
    MOCK_DELAYED_SUCCESS,
    MOCK_NO_RESPONSE,
    MOCK_MALFORMED_RESPONSE,
    MOCK_LATE_RESPONSE,
    MOCK_START_FAILURE,
    MOCK_ACTIVE_FAILURE
} mock_behavior_t;

typedef struct mock_plan {
    mock_behavior_t behavior;
    core_tick_t delay;
} mock_plan_t;

typedef struct mock_event_slot {
    int used;
    backend_event_t event;
    core_deadline_t due;
} mock_event_slot_t;

typedef struct mock_backend {
    mock_plan_t plans[MOCK_PLAN_CAPACITY];
    unsigned int plan_count;
    unsigned int plan_next;
    mock_event_slot_t events[MOCK_EVENT_CAPACITY];
    int open_failures_remaining;
    int recovery_failures_remaining;
    int reconfigure_failures_remaining;
    int rollback_failures_remaining;
    int stop_failures_remaining;
    unsigned int starts;
    unsigned int cancels;
    unsigned int opens;
    unsigned int recovers;
    unsigned int reconfigures;
    unsigned int stops;
    unsigned int last_cancelled_generation;
    unsigned int active_revision;
} mock_backend_t;

void mock_backend_init(mock_backend_t *mock);
int mock_backend_add_plan(mock_backend_t *mock, mock_behavior_t behavior,
                          core_tick_t delay);
const core_backend_ops_t *mock_backend_ops(void);
unsigned int mock_backend_pending_events(const mock_backend_t *mock);

#endif
