#ifndef FOURVRS_GATEWAY_NETWORK_PROFILE_H
#define FOURVRS_GATEWAY_NETWORK_PROFILE_H

#include "network/gateway_network_document.h"
#include "network/gateway_network_store.h"

#define GATEWAY_NETWORK_BINDINGS 8U
typedef struct gateway_network_profile {
    gateway_network_settings_t settings;
    unsigned int affinity[GATEWAY_NETWORK_BINDINGS];
    char original_bind[GATEWAY_NETWORK_BINDINGS][GATEWAY_IPV4_TEXT_MAX];
    char interfaces[GATEWAY_NETWORK_FILE_MAX];
    char resolver[GATEWAY_NETWORK_FILE_MAX];
} gateway_network_profile_t;

int gateway_network_profile_encode(const gateway_network_profile_t *profile,
                                   char *out,size_t capacity,size_t *length);
int gateway_network_profile_decode(const char *data,size_t length,
                                   gateway_network_profile_t *profile);
int gateway_network_profile_candidate(const gateway_network_profile_t *previous,
                                      const gateway_network_settings_t *settings,
                                      gateway_network_profile_t *candidate);

int gateway_network_profile_broadcast(const gateway_network_profile_t *,unsigned int lan,char output[16]);
#endif
