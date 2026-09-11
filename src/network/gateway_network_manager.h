#ifndef FOURVRS_GATEWAY_NETWORK_MANAGER_H
#define FOURVRS_GATEWAY_NETWORK_MANAGER_H

#include "core/deadline.h"
#include "network/gateway_network_settings.h"

#define GATEWAY_NETWORK_CONFIRM_MS 60000UL
#define GATEWAY_NETWORK_APPLY_MS 30000UL
#define GATEWAY_NETWORK_ROLLBACK_MS 30000UL

typedef enum gateway_network_state {
    GATEWAY_NETWORK_UNAVAILABLE = 0,
    GATEWAY_NETWORK_IDLE,
    GATEWAY_NETWORK_APPLYING,
    GATEWAY_NETWORK_WAIT_BINDINGS,
    GATEWAY_NETWORK_WAIT_CONFIRM,
    GATEWAY_NETWORK_COMMITTING,
    GATEWAY_NETWORK_ROLLING_BACK,
    GATEWAY_NETWORK_ROLLBACK_BINDINGS,
    GATEWAY_NETWORK_KEPT,
    GATEWAY_NETWORK_REVERTED,
    GATEWAY_NETWORK_FAILED,
    GATEWAY_NETWORK_ROLLBACK_FAILED,
    GATEWAY_NETWORK_DURABILITY_UNCERTAIN
} gateway_network_state_t;

typedef enum gateway_network_rollback_reason {
    GATEWAY_NETWORK_REASON_NONE = 0,
    GATEWAY_NETWORK_REASON_PEER,
    GATEWAY_NETWORK_REASON_REVERT,
    GATEWAY_NETWORK_REASON_DEADLINE,
    GATEWAY_NETWORK_REASON_STAGE,
    GATEWAY_NETWORK_REASON_APPLY,
    GATEWAY_NETWORK_REASON_OBSERVATION,
    GATEWAY_NETWORK_REASON_COMMIT
} gateway_network_rollback_reason_t;

/* The production implementation lives in an independent process. Every
 * callback has bounded work; async polls return 0 pending, 1 ready, -1 failed.
 * No active mutation may occur in stage(). restore() reads confirmed storage,
 * never a client's RAM baseline. */
typedef struct gateway_network_manager_ops {
    int (*stage)(void *, const gateway_network_settings_t *);
    int (*apply)(void *);
    int (*poll)(void *);
    int (*commit)(void *); /* 1 pending, 0 durable, -1 not committed, -2 uncertain */
    int (*restore)(void *);
    int (*discard)(void *);
} gateway_network_manager_ops_t;

typedef struct gateway_network_manager {
    gateway_network_state_t state;
    const gateway_network_manager_ops_t *ops;
    void *context;
    core_deadline_t deadline;
    unsigned int client_alive;
    unsigned int confirmation_requested;
    unsigned int rollback_requested;
    unsigned int bindings_ready;
    unsigned int generation;
    gateway_network_rollback_reason_t rollback_reason;
    int error_code; /* Provider result, not an errno unless explicitly supplied. */
} gateway_network_manager_t;

int gateway_network_manager_init(gateway_network_manager_t *manager,
                                 const gateway_network_manager_ops_t *ops, void *context);
int gateway_network_manager_apply(gateway_network_manager_t *manager,
                                  const gateway_network_settings_t *candidate, core_tick_t now);
int gateway_network_manager_confirm(gateway_network_manager_t *manager);
void gateway_network_manager_revert(gateway_network_manager_t *manager);
void gateway_network_manager_step(gateway_network_manager_t *manager, core_tick_t now);
unsigned int gateway_network_manager_busy(const gateway_network_manager_t *manager);
const char *gateway_network_state_name(gateway_network_state_t state);

#endif
