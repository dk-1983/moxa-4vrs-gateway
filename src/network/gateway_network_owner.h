#ifndef FOURVRS_NETWORK_OWNER_H
#define FOURVRS_NETWORK_OWNER_H
#include "network/gateway_network_profile.h"
#include "network/gateway_dhcp_client.h"
/* Single policy owner. Providers never execute a DHCP hook; apply/cancel/poll
 * serialize mutators and must acknowledge reaping before another write starts. */
typedef struct gateway_network_owner_ops {
    int (*observe)(void *,gateway_network_observation_t *);
    int (*open)(void *,unsigned int);
    void (*close)(void *,unsigned int);
    int (*receive)(void *,unsigned int,unsigned char *,size_t);
    int (*send)(void *,unsigned int,gateway_dhcp_client_t *,core_tick_t);
    int (*probe)(void *,unsigned int,const gateway_dhcp_client_t *,unsigned int);
    int (*apply)(void *,const gateway_network_observation_t *,const char *);
    int (*poll)(void *); /* 0=pending, 1=reaped success, -1=reaped failure */
    int (*cancel)(void *);
} gateway_network_owner_ops_t;
typedef struct gateway_network_owner {
    const gateway_network_owner_ops_t *ops;void *context;
    gateway_network_profile_t policy;
    gateway_dhcp_client_t dhcp[2];
    gateway_network_observation_t observed,target,writing;
    char resolver[GATEWAY_NETWORK_FILE_MAX];
    core_tick_t next_observe,retry_at,probe_at[2],job_deadline;
    unsigned int opened[2],generation,job_generation,job,canceling,dirty,ready,settled,error,active;
    uint32_t next_xid;
    unsigned int stopping;
} gateway_network_owner_t;
int gateway_network_owner_init(gateway_network_owner_t *,const gateway_network_owner_ops_t *,void *,uint32_t seed);
int gateway_network_owner_policy(gateway_network_owner_t *,const gateway_network_profile_t *,core_tick_t);
void gateway_network_owner_step(gateway_network_owner_t *,core_tick_t);
void gateway_network_owner_stop(gateway_network_owner_t *);
#endif
