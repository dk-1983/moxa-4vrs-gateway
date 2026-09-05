#ifndef FOURVRS_NETWORK_SYSTEM_H
#define FOURVRS_NETWORK_SYSTEM_H
#include "network/gateway_network_observation.h"
/* These providers run only in an isolated, deadline-supervised mutator.
 * Empty IPv4 means deconfigure that LAN, never bind listeners to wildcard. */
int gateway_network_system_write(const gateway_network_observation_t *desired,
    const char *resolver_path,const char *resolver);
int gateway_network_file_replace(const char *path,const char *text);
#endif
