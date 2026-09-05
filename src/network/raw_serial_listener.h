#ifndef FOURVRS_RAW_SERIAL_LISTENER_H
#define FOURVRS_RAW_SERIAL_LISTENER_H

#include <netinet/in.h>
#include "uart/uart_backend.h"

#define RAW_BUFFER_SIZE 1024U
#define RAW_NETWORK_BUDGET 4U
#define RAW_UDP_IDLE_MS 30000U
#define RAW_STALL_MS 5000U
#define RAW_PACKET_GAP_MS 10U
#define RAW_OWNER_GUARD_MS 100U
#define RAW_EOF_IDLE_MS 1000U

typedef struct raw_serial_stats {
    unsigned int network_received, network_sent, serial_read, serial_written;
    unsigned int rejected_peers, dropped_datagrams, discarded_serial;
    unsigned int io_errors, stalls, releases;
} raw_serial_stats_t;

typedef struct raw_serial_listener {
    int fd, client_fd, udp, owned, eof, guard, flush_pending;
    struct sockaddr_in peer;
    uart_backend_t *uart;
    port_runtime_t *port;
    unsigned char tx[RAW_BUFFER_SIZE], rx[RAW_BUFFER_SIZE];
    unsigned int tx_length, rx_length;
    core_tick_t now, activity_at, tx_progress_at, rx_progress_at;
    core_tick_t serial_at, quiet_at;
    raw_serial_stats_t stats;
} raw_serial_listener_t;

void raw_serial_listener_init(raw_serial_listener_t *l, port_runtime_t *port,
                              uart_backend_t *uart, int udp);
int raw_serial_listener_open(raw_serial_listener_t *l, const char *address,
                             unsigned short port);
void raw_serial_listener_step(raw_serial_listener_t *l, core_tick_t now);
int raw_serial_listener_close(raw_serial_listener_t *l);

#endif
