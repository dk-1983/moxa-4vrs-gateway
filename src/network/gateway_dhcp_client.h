#ifndef FOURVRS_DHCP_CLIENT_H
#define FOURVRS_DHCP_CLIENT_H
#include <stdint.h>
#include "core/deadline.h"
#include "network/gateway_network_observation.h"
/* Original bounded DHCP client; packet I/O and address ownership are external.
 * No scripts, environment evaluation, configuration files or host clocks. */
typedef enum gateway_dhcp_state {
    GATEWAY_DHCP_OFF=0,GATEWAY_DHCP_SELECTING,GATEWAY_DHCP_REQUESTING,
    GATEWAY_DHCP_PROBING,GATEWAY_DHCP_BOUND,GATEWAY_DHCP_RENEWING,
    GATEWAY_DHCP_REBINDING,GATEWAY_DHCP_BACKOFF
} gateway_dhcp_state_t;
typedef struct gateway_dhcp_client {
    gateway_dhcp_state_t state;
    gateway_dhcp_lease_t lease;
    unsigned char mac[6];uint32_t xid,server;
    core_tick_t requested_at;unsigned int have_request;
    core_tick_t started,renew_at,rebind_at,expires_at,next_send;
    unsigned int retries,send_type,valid,changed,probes,error;
} gateway_dhcp_client_t;
void gateway_dhcp_start(gateway_dhcp_client_t *,const unsigned char mac[6],uint32_t,core_tick_t);
void gateway_dhcp_stop(gateway_dhcp_client_t *);
void gateway_dhcp_step(gateway_dhcp_client_t *,core_tick_t,unsigned int link);
int gateway_dhcp_receive(gateway_dhcp_client_t *,const unsigned char *,size_t,core_tick_t);
size_t gateway_dhcp_packet(gateway_dhcp_client_t *,unsigned char *,size_t,core_tick_t);
int gateway_dhcp_probed(gateway_dhcp_client_t *,core_tick_t);
void gateway_dhcp_conflict(gateway_dhcp_client_t *,core_tick_t);
#endif
