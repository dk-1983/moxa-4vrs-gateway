#ifndef FOURVRS_UART_BACKEND_H
#define FOURVRS_UART_BACKEND_H

#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

#include "core/port_runtime.h"

#define UART_PORT_COUNT 8
#define UART_IO_BUDGET 64
#define UART_RX_CAPACITY CORE_TRANSACTION_PAYLOAD_MAX
#define UART_RECOVERY_DRAIN_LIMIT 512
#define UART_TRACE_BYTES_MAX UART_IO_BUDGET

#define FOURVRS_MOXA_SET_OP_MODE 0x442UL
#define FOURVRS_MOXA_GET_OP_MODE 0x443UL
#define FOURVRS_MOXA_SET_SPECIAL_BAUD_RATE 0x444UL
#define FOURVRS_MOXA_GET_SPECIAL_BAUD_RATE 0x445UL

typedef struct uart_syscalls {
    int (*open_fn)(void *context, const char *path, int flags);
    int (*close_fn)(void *context, int fd);
    ssize_t (*read_fn)(void *context, int fd, void *buffer, size_t length);
    ssize_t (*write_fn)(void *context, int fd, const void *buffer, size_t length);
    int (*ioctl_fn)(void *context, int fd, unsigned long request, void *argument);
    int (*tcgetattr_fn)(void *context, int fd, struct termios *value);
    int (*tcsetattr_fn)(void *context, int fd, int action, const struct termios *value);
    int (*tcflush_fn)(void *context, int fd, int selector);
    int (*last_error_fn)(void *context);
} uart_syscalls_t;

typedef struct uart_backend_stats {
    unsigned int bytes_read;
    unsigned int bytes_written;
    unsigned int short_writes;
    unsigned int partial_reads;
    unsigned int read_calls;
    unsigned int write_calls;
    unsigned int temporary_errors;
    unsigned int hard_errors;
    unsigned int rx_overflows;
    unsigned int recovery_drained;
    unsigned int opens;
    unsigned int closes;
} uart_backend_stats_t;

typedef enum uart_frame_result {
    UART_FRAME_INCOMPLETE = 0,
    UART_FRAME_COMPLETE = 1,
    UART_FRAME_MALFORMED = -1
} uart_frame_result_t;

typedef uart_frame_result_t (*uart_frame_probe_fn)(
    void *context, const unsigned char *request, unsigned int request_length,
    const unsigned char *response, unsigned int response_length,
    unsigned int *frame_length);

typedef enum uart_read_phase {
    UART_READ_RESPONSE = 0,
    UART_READ_RECOVERY
} uart_read_phase_t;

typedef struct uart_read_trace_event {
    unsigned long monotonic_us;
    unsigned long tx_completed_us;
    unsigned long elapsed_since_tx_us;
    core_tick_t now;
    core_tick_t tx_completed_at;
    core_tick_t elapsed_since_tx;
    unsigned int transaction_id;
    unsigned int generation;
    unsigned int rx_length_before;
    unsigned int rx_length_after;
    unsigned int byte_count;
    unsigned int tx_complete;
    unsigned int recovery_since_previous_transaction;
    uart_read_phase_t phase;
    int result;
    int error;
    unsigned char bytes[UART_TRACE_BYTES_MAX];
} uart_read_trace_event_t;

typedef void (*uart_read_trace_fn)(void *context,
                                   const uart_read_trace_event_t *event);
typedef unsigned long (*uart_trace_clock_fn)(void *context);

typedef struct uart_backend {
    unsigned int port_index;
    char device_path[16];
    int fd;
    int configured;
    core_port_config_t config;
    struct termios saved_termios;
    int saved_termios_valid;
    unsigned int saved_mode;
    int saved_mode_valid;
    unsigned int saved_special_baud;
    int saved_special_baud_valid;
    unsigned char tx[CORE_TRANSACTION_PAYLOAD_MAX];
    unsigned int tx_length;
    unsigned int tx_offset;
    unsigned char rx[UART_RX_CAPACITY];
    unsigned int rx_length;
    unsigned int expected_response_length;
    unsigned int active_generation;
    unsigned int active_transaction_id;
    int transmit_reported;
    core_tick_t tx_completed_at;
    unsigned long tx_completed_us;
    unsigned int recovery_sequence;
    unsigned int recovery_at_previous_start;
    unsigned int recovery_since_previous_transaction;
    core_operation_t operation;
    uart_frame_probe_fn frame_probe;
    void *frame_probe_context;
    uart_backend_stats_t stats;
    const uart_syscalls_t *sys;
    void *sys_context;
    int trace_enabled;
    uart_read_trace_fn read_trace;
    void *read_trace_context;
    uart_trace_clock_fn trace_clock;
    void *trace_clock_context;
} uart_backend_t;

int uart_backend_init(uart_backend_t *uart, unsigned int port_index,
                      const core_port_config_t *config,
                      const uart_syscalls_t *syscalls, void *sys_context);
int uart_config_validate(const core_port_config_t *config);
const core_backend_ops_t *uart_backend_ops(void);
const uart_syscalls_t *uart_posix_syscalls(void);
void uart_backend_set_trace(uart_backend_t *uart, int enabled);
void uart_backend_set_read_trace(uart_backend_t *uart,
                                 uart_read_trace_fn trace, void *context);
void uart_backend_set_trace_clock(uart_backend_t *uart,
                                  uart_trace_clock_fn clock, void *context);
void uart_backend_set_frame_probe(uart_backend_t *uart,
                                  uart_frame_probe_fn probe, void *context);

#endif
