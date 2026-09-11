#ifndef FOURVRS_MODBUS_UDP_LISTENER_H
#define FOURVRS_MODBUS_UDP_LISTENER_H

#include <netinet/in.h>
#include "modbus/modbus_tcp_adapter.h"

#define MODBUS_OWNER_UDP 2U
#define MODBUS_UDP_PEERS 8U
#define MODBUS_UDP_STEP_MAX 8U
#define MODBUS_UDP_QUEUE_TIMEOUT 1000U
#define MODBUS_UDP_RESPONSE_TIMEOUT 1000U
#define MODBUS_UDP_CACHE_TIMEOUT 5000U
#define MODBUS_UDP_IDLE_TIMEOUT 30000U

typedef struct modbus_udp_peer {
    struct sockaddr_in address;
    unsigned int used, epoch, pending, submitted, transaction_id;
    unsigned int rtu_length, response_length, cached;
    unsigned short tid;
    unsigned char rtu[MODBUS_RTU_ADU_MAX];
    unsigned char response[MBAP_ADU_MAX];
    core_tick_t admitted_at, last_seen, completed_at;
} modbus_udp_peer_t;

typedef struct modbus_udp_stats {
    unsigned int received, accepted, sent, malformed, crc_errors;
    unsigned int busy, no_peer, duplicates, conflicts, send_errors;
    unsigned int timeouts, failures, stale, invalid_responses;
} modbus_udp_stats_t;

typedef struct modbus_udp_listener {
    int fd;
    unsigned int rtu_mode, next_epoch, next_peer;
    port_runtime_t *port;
    core_tick_t now;
    modbus_udp_peer_t peers[MODBUS_UDP_PEERS];
    modbus_udp_stats_t stats;
} modbus_udp_listener_t;

void modbus_udp_listener_init(modbus_udp_listener_t *, port_runtime_t *, int);
int modbus_udp_listener_open(modbus_udp_listener_t *, const char *, unsigned short);
void modbus_udp_listener_step_io(modbus_udp_listener_t *, core_tick_t);
void modbus_udp_listener_close(modbus_udp_listener_t *);
unsigned int modbus_udp_listener_peer_count(const modbus_udp_listener_t *);
/* Shared by socket admission and deterministic malformed/flood tests. */
int modbus_udp_listener_receive(modbus_udp_listener_t *, const struct sockaddr_in *,
                                const unsigned char *, unsigned int, core_tick_t);

#endif
