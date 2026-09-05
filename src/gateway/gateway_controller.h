#ifndef FOURVRS_GATEWAY_CONTROLLER_H
#define FOURVRS_GATEWAY_CONTROLLER_H

#include "config/config_model.h"
#include "core/port_runtime.h"

#define GATEWAY_PORT_COUNT 8U
#define GATEWAY_START_TIMEOUT 1000U
#define GATEWAY_RECONFIGURE_TIMEOUT 1000U
#define GATEWAY_STOP_TIMEOUT 1000U
#define GATEWAY_BIND_ADDRESS_LENGTH 40U

typedef enum gateway_port_lifecycle {
    GATEWAY_PORT_DISABLED = 0,
    GATEWAY_PORT_STARTING,
    GATEWAY_PORT_READY,
    GATEWAY_PORT_RECONFIGURING,
    GATEWAY_PORT_ROLLING_BACK,
    GATEWAY_PORT_STOPPING,
    GATEWAY_PORT_DEGRADED,
    GATEWAY_PORT_ERROR,
    GATEWAY_PORT_NETWORK_WAIT
} gateway_port_lifecycle_t;

typedef enum gateway_error {
    GATEWAY_ERROR_NONE = 0,
    GATEWAY_ERROR_INVALID_CONFIG,
    GATEWAY_ERROR_DUPLICATE_UART,
    GATEWAY_ERROR_DUPLICATE_ENDPOINT,
    GATEWAY_ERROR_UNSUPPORTED_TRANSPORT,
    GATEWAY_ERROR_BACKEND_START,
    GATEWAY_ERROR_LISTENER_START,
    GATEWAY_ERROR_RECONFIGURE,
    GATEWAY_ERROR_ROLLBACK,
    GATEWAY_ERROR_STOP
} gateway_error_t;

typedef struct gateway_port_config {
    int enabled;
    unsigned int uart_index;
    serial_mode_t mode;
    unsigned long baud;
    unsigned int data_bits;
    parity_mode_t parity;
    unsigned int stop_bits;
    transport_type_t transport;
    char bind_address[GATEWAY_BIND_ADDRESS_LENGTH];
    /* 0=legacy imported mapping, 1=literal, 2=LAN1, 3=LAN2. Stored atomically
     * with ordinary port edits so an old network mapping cannot resurrect. */
    unsigned int bind_policy;
    unsigned int endpoint_port;
    int special_baud_enabled;
    unsigned long special_baud;
    unsigned int revision;
} gateway_port_config_t;

typedef struct gateway_transport_diagnostics {
    unsigned int clients;
    unsigned int malformed_mbap;
    unsigned int tx_overflow;
    unsigned int write_deadline_expired;
    unsigned int gateway_target_no_response;
    unsigned int gateway_path_unavailable;
    unsigned int crc_failures;
    unsigned int framing_failures;
    unsigned int unit_mismatches;
    unsigned int function_mismatches;
    unsigned int leading_garbage;
    unsigned int raw_network_received, raw_network_sent;
    unsigned int raw_serial_read, raw_serial_written;
    unsigned int raw_rejected_peers, raw_discarded_serial;
} gateway_transport_diagnostics_t;

typedef struct gateway_transport_ops {
    int (*start)(void *context, const gateway_port_config_t *config);
    void (*step)(void *context, core_tick_t now);
    int (*stop)(void *context);
    unsigned int (*client_count)(const void *context);
    unsigned int (*crc_errors)(const void *context);
    unsigned int (*framing_errors)(const void *context);
    void (*diagnostics)(const void *context,
                        gateway_transport_diagnostics_t *diagnostics);
} gateway_transport_ops_t;

typedef struct gateway_port_binding {
    const core_backend_ops_t *backend_ops;
    void *backend_context;
    const gateway_transport_ops_t *transport_ops;
    void *transport_context;
} gateway_port_binding_t;

typedef struct gateway_port_controller {
    gateway_port_config_t current;
    gateway_port_config_t staged;
    gateway_port_config_t known_good;
    port_runtime_t runtime;
    gateway_port_binding_t binding;
    gateway_port_lifecycle_t lifecycle;
    gateway_error_t last_error;
    unsigned int config_generation;
    unsigned int listener_owned;
    unsigned int pending_disable;
    unsigned int listener_rebind;
    unsigned int network_wait;
    unsigned int startup_failures;
    unsigned int listener_failures;
    unsigned int reconfigure_failures;
    unsigned int rollback_failures;
    unsigned int stop_failures;
    unsigned int scheduler_steps;
} gateway_port_controller_t;

typedef struct gateway_controller {
    gateway_port_controller_t ports[GATEWAY_PORT_COUNT];
    unsigned int scheduler_steps;
    unsigned int next_port;
} gateway_controller_t;

typedef struct gateway_port_health {
    unsigned int port_number;
    int enabled;
    gateway_port_lifecycle_t lifecycle;
    gateway_error_t last_error;
    gateway_port_config_t config;
    port_state_t runtime_state;
    port_statistics_t transactions;
    unsigned int connected_clients;
    unsigned int crc_errors;
    unsigned int framing_errors;
    gateway_transport_diagnostics_t transport;
    char runtime_last_error[CORE_ERROR_LENGTH];
    unsigned int config_generation;
} gateway_port_health_t;

typedef struct gateway_health {
    unsigned int configured_ports;
    unsigned int enabled_ports;
    unsigned int ready_ports;
    unsigned int degraded_or_error_ports;
    unsigned int active_clients;
    unsigned int accepted_transactions;
    unsigned int completed_transactions;
    gateway_port_health_t port[GATEWAY_PORT_COUNT];
} gateway_health_t;

void gateway_configuration_defaults(gateway_port_config_t ports[GATEWAY_PORT_COUNT]);
int gateway_configuration_validate(const gateway_port_config_t ports[GATEWAY_PORT_COUNT],
                                   gateway_error_t errors[GATEWAY_PORT_COUNT]);
int gateway_controller_init(gateway_controller_t *controller,
                            const gateway_port_config_t ports[GATEWAY_PORT_COUNT],
                            const gateway_port_binding_t bindings[GATEWAY_PORT_COUNT]);
void gateway_controller_start(gateway_controller_t *controller, core_tick_t now);
void gateway_controller_step(gateway_controller_t *controller, core_tick_t now);
int gateway_controller_reconfigure(gateway_controller_t *controller,
                                   unsigned int port_index,
                                   const gateway_port_config_t *config,
                                   core_tick_t now);
void gateway_controller_shutdown(gateway_controller_t *controller, core_tick_t now);
int gateway_controller_network_wait(gateway_controller_t *,unsigned int index,unsigned int wait,core_tick_t now);
void gateway_controller_health(const gateway_controller_t *controller,
                               gateway_health_t *health);
unsigned int gateway_port_controller_memory_bytes(void);
unsigned int gateway_controller_memory_bytes(void);

#endif
