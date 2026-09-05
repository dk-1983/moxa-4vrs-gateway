#include "uart/uart_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

static void increment(unsigned int *value)
{
    if (*value != ~0U)
        ++*value;
}

static void add_counter(unsigned int *value, unsigned int amount)
{
    if (~0U - *value < amount)
        *value = ~0U;
    else
        *value += amount;
}

static void trace_read(uart_backend_t *uart, core_tick_t now,
                       uart_read_phase_t phase, ssize_t result, int error,
                       unsigned int before, const unsigned char *bytes)
{
    uart_read_trace_event_t event;
    unsigned int count = 0;
    if (uart->read_trace == 0)
        return;
    memset(&event, 0, sizeof(event));
    if (uart->trace_clock != 0) {
        event.monotonic_us = uart->trace_clock(uart->trace_clock_context);
        event.tx_completed_us = uart->tx_completed_us;
        event.elapsed_since_tx_us = event.monotonic_us - event.tx_completed_us;
    }
    event.now = now;
    event.tx_completed_at = uart->tx_completed_at;
    event.elapsed_since_tx = uart->tx_completed_at == 0U ? 0U :
                             core_elapsed(uart->tx_completed_at, now);
    event.transaction_id = uart->active_transaction_id;
    event.generation = uart->active_generation;
    event.rx_length_before = before;
    event.rx_length_after = before;
    event.phase = phase;
    event.result = (int)result;
    event.error = error;
    event.tx_complete = uart->tx_offset == uart->tx_length;
    event.recovery_since_previous_transaction =
        uart->recovery_since_previous_transaction;
    if (result > 0) {
        count = (unsigned int)result;
        if (count > UART_TRACE_BYTES_MAX)
            count = UART_TRACE_BYTES_MAX;
        event.byte_count = count;
        if (phase == UART_READ_RESPONSE)
            event.rx_length_after = before + (unsigned int)result;
        if (bytes != 0)
            memcpy(event.bytes, bytes, count);
    }
    uart->read_trace(uart->read_trace_context, &event);
}

static speed_t standard_speed(unsigned int baud)
{
    switch (baud) {
    case 50: return B50; case 75: return B75; case 110: return B110;
    case 134: return B134; case 150: return B150; case 200: return B200;
    case 300: return B300; case 600: return B600; case 1200: return B1200;
    case 1800: return B1800; case 2400: return B2400; case 4800: return B4800;
    case 9600: return B9600; case 19200: return B19200;
    case 38400: return B38400; case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B500000
    case 500000: return B500000;
#endif
#ifdef B576000
    case 576000: return B576000;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return (speed_t)0;
    }
}

int uart_config_validate(const core_port_config_t *config)
{
    if (config == 0 || config->mode > 3 || config->data_bits < 5 ||
        config->data_bits > 8 || config->parity > 2 ||
        (config->stop_bits != 1 && config->stop_bits != 2))
        return -1;
    if (config->special_baud_enabled)
        return config->special_baud > 0 ? 0 : -1;
    return standard_speed(config->baud) != (speed_t)0 ? 0 : -1;
}

static int temporary_error(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

static int apply_config(uart_backend_t *uart, const core_port_config_t *config)
{
    struct termios value;
    speed_t speed;
    unsigned int mode = config->mode;
    unsigned int readback = ~0U;
    unsigned int special;

    if (uart_config_validate(config) != 0 || uart->fd < 0)
        return -1;
    if (uart->sys->tcgetattr_fn(uart->sys_context, uart->fd, &value) != 0)
        return -1;
    value.c_iflag = 0;
    value.c_oflag = 0;
    value.c_lflag = 0;
    value.c_cflag = CREAD | CLOCAL;
    value.c_cc[VMIN] = 0;
    value.c_cc[VTIME] = 0;
    switch (config->data_bits) {
    case 5: value.c_cflag |= CS5; break;
    case 6: value.c_cflag |= CS6; break;
    case 7: value.c_cflag |= CS7; break;
    default: value.c_cflag |= CS8; break;
    }
    if (config->parity != 0) {
        value.c_cflag |= PARENB;
        value.c_iflag |= INPCK;
        if (config->parity == 2)
            value.c_cflag |= PARODD;
    }
    if (config->stop_bits == 2)
        value.c_cflag |= CSTOPB;

    speed = standard_speed(config->baud);
    if (config->special_baud_enabled) {
#ifdef B4000000
        speed = B4000000;
#else
        return -1;
#endif
    }
    if (cfsetispeed(&value, speed) != 0 || cfsetospeed(&value, speed) != 0)
        return -1;
    if (uart->sys->tcsetattr_fn(uart->sys_context, uart->fd, TCSANOW, &value) != 0)
        return -1;
    if (uart->sys->ioctl_fn(uart->sys_context, uart->fd, FOURVRS_MOXA_SET_OP_MODE, &mode) != 0 ||
        uart->sys->ioctl_fn(uart->sys_context, uart->fd, FOURVRS_MOXA_GET_OP_MODE, &readback) != 0 ||
        readback != mode)
        return -1;
    if (config->special_baud_enabled) {
        special = config->special_baud;
        if (uart->sys->ioctl_fn(uart->sys_context, uart->fd,
                FOURVRS_MOXA_SET_SPECIAL_BAUD_RATE, &special) != 0)
            return -1;
        readback = 0;
        if (uart->sys->ioctl_fn(uart->sys_context, uart->fd,
                FOURVRS_MOXA_GET_SPECIAL_BAUD_RATE, &readback) != 0 ||
            readback != special)
            return -1;
    }
    uart->config = *config;
    uart->configured = 1;
    return 0;
}

static int backend_open(void *context, core_tick_t now)
{
    uart_backend_t *uart = (uart_backend_t *)context;
    (void)now;
    if (uart->fd >= 0)
        return 0;
    uart->fd = uart->sys->open_fn(uart->sys_context, uart->device_path,
                                  O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (uart->fd < 0)
        return -1;
    increment(&uart->stats.opens);
    uart->saved_termios_valid =
        uart->sys->tcgetattr_fn(uart->sys_context, uart->fd, &uart->saved_termios) == 0;
    uart->saved_mode_valid = uart->sys->ioctl_fn(
        uart->sys_context, uart->fd, FOURVRS_MOXA_GET_OP_MODE,
        &uart->saved_mode) == 0;
#ifdef B4000000
    if (uart->saved_termios_valid &&
        cfgetispeed(&uart->saved_termios) == B4000000) {
        uart->saved_special_baud_valid = uart->sys->ioctl_fn(
            uart->sys_context, uart->fd,
            FOURVRS_MOXA_GET_SPECIAL_BAUD_RATE,
            &uart->saved_special_baud) == 0;
    }
#endif
    if (!uart->saved_termios_valid || !uart->saved_mode_valid) {
        uart->sys->close_fn(uart->sys_context, uart->fd);
        uart->fd = -1;
        increment(&uart->stats.closes);
        return -1;
    }
#ifdef B4000000
    if (cfgetispeed(&uart->saved_termios) == B4000000 &&
        !uart->saved_special_baud_valid) {
        uart->sys->close_fn(uart->sys_context, uart->fd);
        uart->fd = -1;
        increment(&uart->stats.closes);
        return -1;
    }
#endif
    if (apply_config(uart, &uart->config) != 0) {
        if (uart->saved_termios_valid)
            uart->sys->tcsetattr_fn(uart->sys_context, uart->fd, TCSANOW,
                                    &uart->saved_termios);
        if (uart->saved_mode_valid)
            uart->sys->ioctl_fn(uart->sys_context, uart->fd,
                                FOURVRS_MOXA_SET_OP_MODE,
                                &uart->saved_mode);
        uart->sys->close_fn(uart->sys_context, uart->fd);
        uart->fd = -1;
        increment(&uart->stats.closes);
        return -1;
    }
    return 0;
}

static int backend_start(void *context, const core_transaction_t *transaction,
                         core_tick_t now)
{
    uart_backend_t *uart = (uart_backend_t *)context;
    (void)now;
    if (uart->fd < 0 || transaction->payload_length == 0 ||
        transaction->operation.expected_response_length > UART_RX_CAPACITY)
        return -1;
    uart->operation = transaction->operation;
    if (uart->operation.direction > CORE_OPERATION_TX_THEN_RX ||
        uart->operation.response_policy > CORE_RESPONSE_BACKEND_FRAMED ||
        (uart->operation.response_policy == CORE_RESPONSE_BACKEND_FRAMED &&
         uart->frame_probe == 0))
        return -1;
    memcpy(uart->tx, transaction->payload, transaction->payload_length);
    uart->tx_length = uart->operation.direction == CORE_OPERATION_RX_ONLY ?
                      0 : transaction->payload_length;
    uart->tx_offset = 0;
    uart->rx_length = 0;
    uart->expected_response_length = uart->operation.expected_response_length;
    uart->active_generation = transaction->generation;
    uart->active_transaction_id = transaction->id;
    uart->transmit_reported = 0;
    uart->tx_completed_at = 0;
    uart->tx_completed_us = 0;
    uart->recovery_since_previous_transaction =
        uart->recovery_sequence != uart->recovery_at_previous_start;
    uart->recovery_at_previous_start = uart->recovery_sequence;
    if(uart->trace_enabled){unsigned int i;fprintf(stderr,"RTU_TX core_id=%u generation=%u direction=%u framing=%u bytes=",transaction->id,transaction->generation,(unsigned int)transaction->operation.direction,(unsigned int)transaction->operation.response_policy);for(i=0;i<transaction->payload_length;++i)fprintf(stderr,"%02X",transaction->payload[i]);fprintf(stderr,"\n");}
    return 0;
}

static int backend_poll(void *context, core_tick_t now, backend_event_t *event)
{
    uart_backend_t *uart = (uart_backend_t *)context;
    ssize_t result;
    unsigned int remaining;
    unsigned int budget;
    int error;
    (void)now;
    memset(event, 0, sizeof(*event));
    if (uart->fd < 0)
        return -1;
    if (uart->active_generation == 0)
        return 0;

    if (uart->tx_offset < uart->tx_length) {
        remaining = uart->tx_length - uart->tx_offset;
        budget = remaining < UART_IO_BUDGET ? remaining : UART_IO_BUDGET;
        increment(&uart->stats.write_calls);
        result = uart->sys->write_fn(uart->sys_context, uart->fd, uart->tx + uart->tx_offset, budget);
        if (result > 0) {
            if ((unsigned int)result < budget)
                increment(&uart->stats.short_writes);
            uart->tx_offset += (unsigned int)result;
            add_counter(&uart->stats.bytes_written, (unsigned int)result);
            if (uart->tx_offset == uart->tx_length)
                uart->tx_completed_at = now;
            if (uart->tx_offset == uart->tx_length && uart->trace_clock != 0)
                uart->tx_completed_us = uart->trace_clock(
                    uart->trace_clock_context);
        } else if (result < 0) {
            error = uart->sys->last_error_fn(uart->sys_context);
            if (temporary_error(error))
                increment(&uart->stats.temporary_errors);
            else {
                increment(&uart->stats.hard_errors);
                event->type = BACKEND_EVENT_FAILURE;
                event->generation = uart->active_generation;
                return 1;
            }
        }
        return 0;
    }

    if (uart->operation.direction == CORE_OPERATION_TX_THEN_RX &&
        !uart->transmit_reported) {
        uart->transmit_reported = 1;
        event->type = BACKEND_EVENT_TRANSMITTED;
        event->generation = uart->active_generation;
        return 1;
    }

    if (uart->operation.direction == CORE_OPERATION_TX_ONLY) {
        event->type = BACKEND_EVENT_RESPONSE;
        event->generation = uart->active_generation;
        return 1;
    }

    remaining = UART_RX_CAPACITY - uart->rx_length;
    budget = remaining < UART_IO_BUDGET ? remaining : UART_IO_BUDGET;
    if (budget == 0) {
        increment(&uart->stats.rx_overflows);
        event->type = BACKEND_EVENT_MALFORMED;
        event->generation = uart->active_generation;
        return 1;
    }
    increment(&uart->stats.read_calls);
    {
    unsigned int before = uart->rx_length;
    unsigned char *destination = uart->rx + before;
    result = uart->sys->read_fn(uart->sys_context, uart->fd, destination, budget);
    error = result < 0 ? uart->sys->last_error_fn(uart->sys_context) : 0;
    trace_read(uart, now, UART_READ_RESPONSE, result, error, before,
               destination);
    if (result > 0) {
        if ((unsigned int)result < budget)
            increment(&uart->stats.partial_reads);
        uart->rx_length += (unsigned int)result;
        add_counter(&uart->stats.bytes_read, (unsigned int)result);
        if (uart->operation.response_policy == CORE_RESPONSE_FIXED_LENGTH &&
            uart->rx_length >= uart->expected_response_length) {
            event->type = uart->rx_length == uart->expected_response_length ?
                          BACKEND_EVENT_RESPONSE : BACKEND_EVENT_MALFORMED;
            event->generation = uart->active_generation;
            event->length = uart->rx_length;
            memcpy(event->data, uart->rx, uart->rx_length);
            if(uart->trace_enabled){unsigned int i;fprintf(stderr,"RTU_RX generation=%u bytes=",uart->active_generation);for(i=0;i<uart->rx_length;++i)fprintf(stderr,"%02X",uart->rx[i]);fprintf(stderr,"\n");}
            return 1;
        }
        if (uart->operation.response_policy == CORE_RESPONSE_BACKEND_FRAMED) {
            unsigned int frame_length = 0;
            uart_frame_result_t framed = uart->frame_probe(
                uart->frame_probe_context, uart->tx, uart->tx_length,
                uart->rx, uart->rx_length, &frame_length);
            if (framed != UART_FRAME_INCOMPLETE) {
                event->type = framed == UART_FRAME_COMPLETE &&
                              frame_length == uart->rx_length ?
                              BACKEND_EVENT_RESPONSE : BACKEND_EVENT_MALFORMED;
                event->generation = uart->active_generation;
                event->length = uart->rx_length;
                memcpy(event->data, uart->rx, uart->rx_length);
                if(uart->trace_enabled){unsigned int i;fprintf(stderr,"RTU_RX core_id=%u generation=%u length=%u result=%s bytes=",uart->active_transaction_id,uart->active_generation,uart->rx_length,event->type==BACKEND_EVENT_RESPONSE?"complete":"malformed");for(i=0;i<uart->rx_length;++i)fprintf(stderr,"%02X",uart->rx[i]);fprintf(stderr,"\n");}
                return 1;
            }
        }
    } else if (result < 0) {
        if (temporary_error(error))
            increment(&uart->stats.temporary_errors);
        else {
            increment(&uart->stats.hard_errors);
            event->type = BACKEND_EVENT_FAILURE;
            event->generation = uart->active_generation;
            return 1;
        }
    }
    }
    return 0;
}

static void backend_cancel(void *context, unsigned int generation)
{
    uart_backend_t *uart = (uart_backend_t *)context;
    if (uart->active_generation == generation) {
        uart->tx_length = uart->tx_offset = uart->rx_length = 0;
        uart->expected_response_length = 0;
        uart->active_generation = 0;
        uart->active_transaction_id = 0;
        uart->transmit_reported = 0;
        memset(&uart->operation, 0, sizeof(uart->operation));
    }
}

static int backend_recover(void *context, core_tick_t now)
{
    uart_backend_t *uart = (uart_backend_t *)context;
    unsigned char discard[UART_IO_BUDGET];
    unsigned int total = 0;
    ssize_t result;
    int error;
    (void)now;
    if (uart->fd < 0)
        return -1;
    while (total < UART_RECOVERY_DRAIN_LIMIT) {
        result = uart->sys->read_fn(uart->sys_context, uart->fd, discard, sizeof(discard));
        error = result < 0 ? uart->sys->last_error_fn(uart->sys_context) : 0;
        trace_read(uart, now, UART_READ_RECOVERY, result, error, total,
                   discard);
        if (result > 0) {
            total += (unsigned int)result;
            continue;
        }
        if (result < 0) {
            if (temporary_error(error))
                break;
            increment(&uart->stats.hard_errors);
            return -1;
        }
        break;
    }
    add_counter(&uart->stats.recovery_drained, total);
    increment(&uart->recovery_sequence);
    if (total >= UART_RECOVERY_DRAIN_LIMIT ||
        uart->sys->tcflush_fn(uart->sys_context, uart->fd, TCIFLUSH) != 0)
        return -1;
    uart->tx_length = uart->tx_offset = uart->rx_length = 0;
    uart->expected_response_length = uart->active_generation = 0;
    uart->active_transaction_id = 0;
    uart->transmit_reported = 0;
    memset(&uart->operation, 0, sizeof(uart->operation));
    return 0;
}

static int backend_reconfigure(void *context,
                               const core_port_config_t *config,
                               core_tick_t now)
{
    uart_backend_t *uart = (uart_backend_t *)context;
    (void)now;
    return apply_config(uart, config);
}

static int backend_stop(void *context, core_tick_t now)
{
    uart_backend_t *uart = (uart_backend_t *)context;
    int result = 0;
    (void)now;
    if (uart->fd < 0)
        return 0;
    if (uart->saved_termios_valid &&
        uart->sys->tcsetattr_fn(uart->sys_context, uart->fd, TCSANOW,
                                &uart->saved_termios) != 0)
        result = -1;
    if (uart->saved_mode_valid &&
        uart->sys->ioctl_fn(uart->sys_context, uart->fd,
                            FOURVRS_MOXA_SET_OP_MODE,
                            &uart->saved_mode) != 0)
        result = -1;
    if (uart->saved_special_baud_valid &&
        uart->sys->ioctl_fn(uart->sys_context, uart->fd,
                            FOURVRS_MOXA_SET_SPECIAL_BAUD_RATE,
                            &uart->saved_special_baud) != 0)
        result = -1;
    if (uart->sys->close_fn(uart->sys_context, uart->fd) != 0) {
        result = -1;
    } else {
        uart->fd = -1;
        uart->configured = 0;
        increment(&uart->stats.closes);
    }
    return result;
}

static const core_backend_ops_t operations = {
    backend_open, backend_start, backend_poll, backend_cancel,
    backend_recover, backend_reconfigure, backend_stop
};

int uart_backend_init(uart_backend_t *uart, unsigned int port_index,
                      const core_port_config_t *config,
                      const uart_syscalls_t *syscalls, void *sys_context)
{
    if (uart == 0 || port_index >= UART_PORT_COUNT ||
        uart_config_validate(config) != 0 || syscalls == 0 ||
        syscalls->open_fn == 0 || syscalls->close_fn == 0 ||
        syscalls->read_fn == 0 || syscalls->write_fn == 0 ||
        syscalls->ioctl_fn == 0 || syscalls->tcgetattr_fn == 0 ||
        syscalls->tcsetattr_fn == 0 || syscalls->tcflush_fn == 0 ||
        syscalls->last_error_fn == 0)
        return -1;
    memset(uart, 0, sizeof(*uart));
    uart->port_index = port_index;
    sprintf(uart->device_path, "/dev/ttyM%u", port_index);
    uart->fd = -1;
    uart->config = *config;
    uart->sys = syscalls;
    uart->sys_context = sys_context;
    return 0;
}

const core_backend_ops_t *uart_backend_ops(void)
{
    return &operations;
}

void uart_backend_set_trace(uart_backend_t *uart,int enabled){if(uart)uart->trace_enabled=enabled?1:0;}
void uart_backend_set_read_trace(uart_backend_t *uart,
                                 uart_read_trace_fn trace, void *context)
{ if (uart != 0) { uart->read_trace = trace; uart->read_trace_context = context; } }
void uart_backend_set_trace_clock(uart_backend_t *uart,
                                  uart_trace_clock_fn clock, void *context)
{ if (uart != 0) { uart->trace_clock = clock; uart->trace_clock_context = context; } }
void uart_backend_set_frame_probe(uart_backend_t *uart,
                                  uart_frame_probe_fn probe, void *context)
{ if (uart != 0) { uart->frame_probe = probe; uart->frame_probe_context = context; } }

static int posix_open(void *c, const char *path, int flags) { (void)c; return open(path, flags); }
static int posix_close(void *c, int fd) { (void)c; return close(fd); }
static ssize_t posix_read(void *c, int fd, void *buf, size_t len) { (void)c; return read(fd, buf, len); }
static ssize_t posix_write(void *c, int fd, const void *buf, size_t len) { (void)c; return write(fd, buf, len); }
static int posix_ioctl(void *c, int fd, unsigned long req, void *arg) { (void)c; return ioctl(fd, req, arg); }
static int posix_tcgetattr(void *c, int fd, struct termios *v) { (void)c; return tcgetattr(fd, v); }
static int posix_tcsetattr(void *c, int fd, int action, const struct termios *v) { (void)c; return tcsetattr(fd, action, v); }
static int posix_tcflush(void *c, int fd, int selector) { (void)c; return tcflush(fd, selector); }
static int posix_last_error(void *c) { (void)c; return errno; }

static const uart_syscalls_t posix_calls = {
    posix_open, posix_close, posix_read, posix_write, posix_ioctl,
    posix_tcgetattr, posix_tcsetattr, posix_tcflush, posix_last_error
};

const uart_syscalls_t *uart_posix_syscalls(void)
{
    return &posix_calls;
}
