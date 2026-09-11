#ifndef FOURVRS_GATEWAY_NETWORK_SUPERVISOR_H
#define FOURVRS_GATEWAY_NETWORK_SUPERVISOR_H

#include "network/gateway_network_manager.h"

/* Private socketpair protocol; no network-addressable listener. The caller
 * must exec/fork the supervisor separately and close all unrelated descriptors
 * before invoking this loop. EOF is a failed client, never an implicit Keep.
 * K=Keep, R=Revert, B=bindings ready, D=depart (no binding acknowledgement).
 * One command is consumed per iteration. */
typedef struct gateway_network_supervisor_status {
    gateway_network_state_t state;
    core_tick_t deadline;
    gateway_network_rollback_reason_t rollback_reason;
    int error_code;
} gateway_network_supervisor_status_t;
typedef core_tick_t (*gateway_network_supervisor_clock_fn)(void *);

typedef enum gateway_network_supervisor_event {
    GATEWAY_NET_EVENT_STATE=0, GATEWAY_NET_EVENT_EOF, GATEWAY_NET_EVENT_HUP,
    GATEWAY_NET_EVENT_POLL_ERROR, GATEWAY_NET_EVENT_RECV_ERROR,
    GATEWAY_NET_EVENT_SEND_ERROR, GATEWAY_NET_EVENT_SIGNAL,
    GATEWAY_NET_EVENT_DEPART, GATEWAY_NET_EVENT_FINISH,
    GATEWAY_NET_EVENT_WORKER_START, GATEWAY_NET_EVENT_WORKER_REAP,
    GATEWAY_NET_EVENT_WORKER_KILL, GATEWAY_NET_EVENT_OWNER_STATUS,
    GATEWAY_NET_EVENT_LEASE_LAN1, GATEWAY_NET_EVENT_LEASE_LAN2
} gateway_network_supervisor_event_t;
/* At most 64 records per transaction. Callback must not block; no configuration
 * data is supplied. detail is errno/revents/signal/worker status by event type. */
#define GATEWAY_NETWORK_TRACE_LIMIT 64U
typedef void (*gateway_network_supervisor_trace_fn)(void *,core_tick_t,
    const gateway_network_manager_t *,gateway_network_supervisor_event_t,int,unsigned long);
int gateway_network_supervisor_run_traced(int,gateway_network_manager_t *,
    const gateway_network_settings_t *,gateway_network_supervisor_clock_fn,void *,
    gateway_network_supervisor_trace_fn,void *);

int gateway_network_supervisor_run(int socket_fd,gateway_network_manager_t *manager,
                                   const gateway_network_settings_t *candidate,
                                   gateway_network_supervisor_clock_fn clock,void *clock_context);

#endif
