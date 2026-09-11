#ifndef FOURVRS_WEB_CONTROL_H
#define FOURVRS_WEB_CONTROL_H
#include "app/gateway_application.h"
#define WEB_FOUNDATION_VERSION "v2026.02.01"
typedef enum web_actual { WEB_STOPPED, WEB_STARTING, WEB_RUNNING, WEB_FAILED } web_actual_t;
typedef struct web_status {
    unsigned int saved_enabled, interface;
    web_actual_t actual;
    int error;
    gateway_configuration_transaction_state_t transaction;
} web_status_t;
/* Gateway event-loop API. expected is a server-owned snapshot, not a wire struct.
 * -2 conflict, -3 unauthorized, -1 invalid/busy, 0 accepted/no-op.
 * Authorization comes from a verified local caller/IPC identity, never JSON. */
int web_control_set(gateway_application_t *, const gateway_persistent_config_t *expected,
                    unsigned int enabled, unsigned int interface, int administrator);
void web_control_status(const gateway_application_t *, web_actual_t, int, web_status_t *);
/* Observed IPv4 addresses ONLY, eth0/eth1 in that order; no wildcard fallback.
 * Returns count, or -1 if any selected LAN is absent/invalid. */
int web_control_addresses(unsigned int interface, const char *eth0, const char *eth1,
                          char addresses[2][16]);
/* Redirect is derived from validated accepted local address, never Host. */
int web_control_redirect(const char *local_address, char *out, size_t capacity);
#endif
