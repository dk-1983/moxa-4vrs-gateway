#ifndef FOURVRS_GATEWAY_REAL_ADAPTER_H
#define FOURVRS_GATEWAY_REAL_ADAPTER_H

#include "gateway/gateway_controller.h"
#include "network/modbus_tcp_listener.h"
#include "network/modbus_udp_listener.h"
#include "network/raw_serial_listener.h"
#include "uart/uart_backend.h"

typedef struct gateway_listener_driver {
    void (*init)(void *context, port_runtime_t *runtime);
    int (*open)(void *context, const char *address, unsigned short port);
    void (*step)(void *context, core_tick_t now);
    int (*close)(void *context);
    unsigned int (*clients)(const void *context);
    unsigned int (*crc_errors)(const void *context);
    unsigned int (*framing_errors)(const void *context);
    void (*diagnostics)(const void *context,
                        gateway_transport_diagnostics_t *diagnostics);
} gateway_listener_driver_t;

typedef struct gateway_real_adapter {
    uart_backend_t uart;
    modbus_tcp_listener_t listener;
    modbus_udp_listener_t udp_listener;
    raw_serial_listener_t raw_listener;
    port_runtime_t *runtime;
    const gateway_listener_driver_t *listener_driver;
    void *listener_context;
    char bind_address[GATEWAY_BIND_ADDRESS_LENGTH];
    unsigned int port_index;
    unsigned int listener_started;
    unsigned int builtin_listener;
} gateway_real_adapter_t;

int gateway_real_adapter_init(gateway_real_adapter_t *adapter,
                              unsigned int port_index,
                              const gateway_port_config_t *config,
                              const uart_syscalls_t *uart_syscalls,
                              void *uart_syscall_context,
                              const gateway_listener_driver_t *listener_driver,
                              void *listener_context);
void gateway_real_adapter_attach(gateway_real_adapter_t *adapter,
                                 port_runtime_t *runtime);
gateway_port_binding_t gateway_real_adapter_binding(gateway_real_adapter_t *adapter);
const gateway_listener_driver_t *gateway_modbus_listener_driver(void);

#endif
