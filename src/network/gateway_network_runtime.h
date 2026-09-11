#ifndef FOURVRS_NETWORK_RUNTIME_H
#define FOURVRS_NETWORK_RUNTIME_H
#include "network/gateway_network_profile.h"
#include "network/gateway_network_supervisor.h"
#include "network/gateway_network_observation.h"
#include "network/gateway_network_service.h"

/* Only eth1 address/mask/broadcast/admin state belong to this static stage.
 * Callbacks run in the isolated supervisor, never in the application's loop.
 * Tests provide file-backed fake kernels; production uses typed ioctls. */
typedef struct gateway_network_environment {
    const char *store_directory;
    const char *interfaces_path;
    const char *resolver_path;
    int (*read_lan2)(void *, gateway_lan_observation_t *);
    int (*write_lan2)(void *, const gateway_lan_observation_t *);
    void *context;
    gateway_network_supervisor_clock_fn clock;
    void *clock_context;
    const char *trace_path; /* Optional bounded diagnostic file; never a profile. */
    int (*read_network)(void *,gateway_network_observation_t *);
    int (*write_network)(void *,const gateway_network_observation_t *,const char *resolver);
    const gateway_network_service_environment_t *service;
    const char *import_proc_directory,*import_lease_directory;
} gateway_network_environment_t;

typedef struct gateway_network_runtime {
    const gateway_network_environment_t *environment;
    gateway_network_profile_t confirmed;
    gateway_network_profile_t candidate;
    gateway_network_supervisor_status_t status;
    int fd, pid;
    unsigned int available, binding_index, binding_wait, binding_ack, stop_sent;
    unsigned int binding_affinity[8];
    gateway_network_observation_t observed;
    unsigned int observed_valid, observation_error, stopping;
    int observer_pid, observer_fd;
    unsigned int observer_received, observer_failed; /* Per child, not per poll. */
    core_tick_t observe_at, observer_deadline;
    unsigned int lease_valid[2],dhcp_state[2];
    core_tick_t lease_expiry[2];
    unsigned int observation_epoch,observer_epoch;
    unsigned int dynamic_index,recovering,recovery_kept;
    core_tick_t recovery_deadline;
} gateway_network_runtime_t;

const gateway_network_environment_t *gateway_network_environment_production(void);
int gateway_network_runtime_init(gateway_network_runtime_t *, const gateway_network_environment_t *);
int gateway_network_runtime_start(gateway_network_runtime_t *, const gateway_network_settings_t *);
int gateway_network_runtime_command(gateway_network_runtime_t *, char);
void gateway_network_runtime_poll(gateway_network_runtime_t *);
void gateway_network_runtime_disconnect(gateway_network_runtime_t *);
void gateway_network_runtime_stop(gateway_network_runtime_t *);
int gateway_network_runtime_busy(const gateway_network_runtime_t *);
int gateway_network_enroll_detailed(const gateway_network_environment_t *, const char *const bindings[8], const char **stage);
int gateway_network_enroll(const gateway_network_environment_t *, const char *const bindings[8]);
/* Called before vendor ifup, independent of CompactFlash. Never reads candidate. */
int gateway_network_boot_restore_detailed(const gateway_network_environment_t *,const char **stage);
int gateway_network_boot_restore(const gateway_network_environment_t *);
/* Child-only: detach session and close all unrelated descriptors. */
int gateway_network_process_isolate(int keep_fd);
#endif
