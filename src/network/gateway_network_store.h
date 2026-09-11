#ifndef FOURVRS_GATEWAY_NETWORK_STORE_H
#define FOURVRS_GATEWAY_NETWORK_STORE_H

#include <stddef.h>

#define GATEWAY_NETWORK_SNAPSHOT_MAX 32768U
typedef enum gateway_network_store_result {
    GATEWAY_NET_STORE_OK = 0,
    GATEWAY_NET_STORE_ABSENT = 1,
    GATEWAY_NET_STORE_INVALID = -1,
    GATEWAY_NET_STORE_IO = -2,
    GATEWAY_NET_STORE_UNCERTAIN = -3
} gateway_network_store_result_t;

/* Files contain a length and CRC32 header, never native C structures. The
 * caller serializes the network, route/DNS policy and binding affinities in
 * ONE payload. The lock must cover the whole transaction, not each rename. */
int gateway_network_store_lock(const char *directory);
void gateway_network_store_unlock(int fd);
gateway_network_store_result_t gateway_network_store_read(
    const char *directory, const char *name, char *out, size_t capacity, size_t *length);
gateway_network_store_result_t gateway_network_store_write(
    const char *directory, const char *name, const char *data, size_t length);
/* First adoption is durable before applying anything; never replaces existing
 * confirmed state. Startup reads confirmed, falling back to good on corruption.
 * Candidate is deliberately NEVER a startup fallback. */
gateway_network_store_result_t gateway_network_store_adopt(
    const char *directory, const char *data, size_t length);
gateway_network_store_result_t gateway_network_store_boot(
    const char *directory, char *out, size_t capacity, size_t *length);
gateway_network_store_result_t gateway_network_store_confirm(const char *directory);
gateway_network_store_result_t gateway_network_store_abort_commit(const char *directory);

gateway_network_store_result_t gateway_network_store_discard(const char *directory);

#endif
