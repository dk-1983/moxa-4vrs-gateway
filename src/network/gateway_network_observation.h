#ifndef FOURVRS_NETWORK_OBSERVATION_H
#define FOURVRS_NETWORK_OBSERVATION_H
#include <stddef.h>
#include "network/gateway_network_settings.h"

typedef struct gateway_lan_observation {
    unsigned int present,up,link;
    unsigned char mac[6];
    char address[16],netmask[16],broadcast[16];
} gateway_lan_observation_t;
typedef struct gateway_network_observation {
    gateway_lan_observation_t lan[2];
    /* Unsupported route/DNS policy remains observable but must block Apply. */
    unsigned int unsupported;
    unsigned int default_lan,default_routes;
    char gateway[16];
    char dns[2][16];
} gateway_network_observation_t;
typedef struct gateway_dhcp_lease {
    char address[16],netmask[16],broadcast[16],gateway[16],dns[2][16];
    unsigned long lifetime;
} gateway_dhcp_lease_t;

void gateway_ipv4_format(unsigned long address,char output[16]);
/* Reads only explicit numeric fields. Vendor HOSTNAME/DOMAIN/etc are never
 * evaluated, expanded or passed to a shell. Exact interface identity required. */
int gateway_dhcp_lease_decode(const char *data,size_t length,unsigned int lan,
                              gateway_dhcp_lease_t *lease);
int gateway_network_routes_decode(const char *data,size_t length,
                                   gateway_network_observation_t *observation);
int gateway_network_observe(gateway_network_observation_t *observation);
#endif
