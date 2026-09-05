#ifndef FOURVRS_GATEWAY_NETWORK_DOCUMENT_H
#define FOURVRS_GATEWAY_NETWORK_DOCUMENT_H

#include <stddef.h>
#include "network/gateway_network_settings.h"

#define GATEWAY_NETWORK_FILE_MAX 16384U

/* Import never writes a vendor file or evaluates its hooks. Unsupported input
 * remains observable, but must not be used as an editable baseline. Owned
 * dns-nameserver(s) lists must exactly agree with ordered resolver servers. */
int gateway_network_import(const char *interfaces, size_t interfaces_length,
                           const char *resolver, size_t resolver_length,
                           gateway_network_settings_t *settings);
/* Rewrite only owned IPv4 fields; all other bytes, including hooks, survive.
 * Unsupported imports are rejected, with output length left at zero. */
int gateway_network_render(const char *interfaces, size_t interfaces_length,
                           const gateway_network_settings_t *settings,
                           char *output, size_t capacity, size_t *length);
int gateway_network_resolver(const char *resolver, size_t resolver_length,
                             const char dns[2][GATEWAY_IPV4_TEXT_MAX],
                             char *output, size_t capacity, size_t *length);

#endif
