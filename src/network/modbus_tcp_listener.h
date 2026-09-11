#ifndef FOURVRS_MODBUS_TCP_LISTENER_H
#define FOURVRS_MODBUS_TCP_LISTENER_H

#include "modbus/modbus_dispatcher.h"

#define MODBUS_LISTENER_TEST_PORT 1502U
#define MODBUS_LISTENER_CLIENT_MAX MODBUS_CLIENT_MAX
#define MODBUS_TX_QUEUE_MAX 4U
#define MODBUS_TX_BYTES_MAX (MBAP_ADU_MAX * MODBUS_TX_QUEUE_MAX)
#define MODBUS_SOCKET_IO_STEP_MAX 256U
#define MODBUS_INPUT_TIMEOUT 5000U
#define MODBUS_WRITE_TIMEOUT 5000U
#define MODBUS_IDLE_TIMEOUT 30000U

typedef struct modbus_tx_slot {
    unsigned int length;
    unsigned char data[MBAP_ADU_MAX];
} modbus_tx_slot_t;

typedef struct modbus_tcp_client {
    int fd;
    unsigned int id;
    unsigned int epoch;
    int peer_eof;
    mbap_stream_t rx;
    modbus_tx_slot_t tx[MODBUS_TX_QUEUE_MAX];
    unsigned int tx_head;
    unsigned int tx_count;
    unsigned int tx_offset;
    core_tick_t connected_at;
    core_tick_t last_progress;
    core_tick_t frame_started_at;
    core_tick_t write_started_at;
} modbus_tcp_client_t;

typedef struct modbus_listener_stats {
    unsigned int accepted;
    unsigned int refused;
    unsigned int closed;
    unsigned int malformed;
    unsigned int rx_overflow;
    unsigned int tx_overflow;
    unsigned int write_deadline_expired;
    unsigned int output_peer_errors;
    unsigned int stale_completions;
    unsigned int invalid_responses;
    unsigned int gateway_target_no_response;
    unsigned int gateway_path_unavailable;
    unsigned int timeout_exceptions_queued;
    unsigned int timeout_exceptions_obsolete_epoch;
    unsigned int timeout_exception_output_failures;
    unsigned int downstream_crc_failures;
    unsigned int downstream_framing_failures;
    unsigned int downstream_unit_mismatches;
    unsigned int downstream_function_mismatches;
    unsigned int downstream_leading_garbage;
    unsigned int rx_bytes;
    unsigned int tx_bytes;
    unsigned int scheduler_steps;
    unsigned int max_clients;
} modbus_listener_stats_t;

typedef struct modbus_tcp_listener {
    int listen_fd;
    unsigned int next_epoch;
    modbus_tcp_client_t clients[MODBUS_LISTENER_CLIENT_MAX];
    modbus_dispatcher_t dispatcher;
    port_runtime_t *port;
    core_tick_t now;
    modbus_listener_stats_t stats;
    int trace_enabled;
} modbus_tcp_listener_t;

void modbus_tcp_listener_init(modbus_tcp_listener_t *listener,
                              port_runtime_t *port);
int modbus_tcp_listener_open(modbus_tcp_listener_t *listener,
                             const char *address, unsigned short port);
void modbus_tcp_listener_step(modbus_tcp_listener_t *listener,
                              core_tick_t now);
void modbus_tcp_listener_step_io(modbus_tcp_listener_t *listener,
                                 core_tick_t now);
void modbus_tcp_listener_close(modbus_tcp_listener_t *listener);
unsigned int modbus_tcp_listener_client_count(const modbus_tcp_listener_t *listener);
unsigned int modbus_tcp_listener_memory_bytes(void);
int modbus_tcp_listener_queue_response(modbus_tcp_listener_t *listener,
                                       unsigned int client_id,
                                       unsigned int epoch,
                                       const unsigned char *data,
                                       unsigned int length);
void modbus_tcp_listener_set_trace(modbus_tcp_listener_t *listener, int enabled);

#endif
