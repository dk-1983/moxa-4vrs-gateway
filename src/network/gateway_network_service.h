#ifndef FOURVRS_NETWORK_SERVICE_H
#define FOURVRS_NETWORK_SERVICE_H
#include "network/gateway_network_owner.h"
/* The paths are local, root-owned recovery assets, independent of CF. Provider
 * substitution is for isolated host tests; production supplies typed syscalls. */
typedef struct gateway_network_service_environment {
    const char *directory;
    int (*observe)(void *,gateway_network_observation_t *);
    int (*write)(void *,const gateway_network_observation_t *,const char *);
    const gateway_network_owner_ops_t *client_ops;
    void *context;
    core_tick_t (*clock)(void *);
    int *started_guardian; /* Optional local process identity for test harnesses. */
} gateway_network_service_environment_t;
typedef struct gateway_network_service_status {
    unsigned int protocol; /* Reject stale helper layouts on the local socket. */
    unsigned int ready,settled,error,generation,request;
    gateway_network_settings_t policy;
    unsigned int recovery_generation; /* Filled only by the local observer child. */
    gateway_network_observation_t observed;
    gateway_network_observation_t effective;
    unsigned int dhcp_state[2],lease_valid[2];
    core_tick_t lease_expiry[2];
    int owner_pid,guardian_pid;
} gateway_network_service_status_t;
/* Returns a private transaction fd, or a negative error. Commands A=staged,
 * R=confirmed, K=durable commit accepted. Closure always consults confirmed. */
int gateway_network_service_connect(const gateway_network_service_environment_t *,unsigned int start);
int gateway_network_service_command(int,char,unsigned int request);
int gateway_network_service_read(int,gateway_network_service_status_t *);
int gateway_network_service_query(const gateway_network_service_environment_t *,gateway_network_service_status_t *);
const gateway_network_service_environment_t *gateway_network_service_production(void);
#endif
