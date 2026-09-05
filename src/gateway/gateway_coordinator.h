#ifndef FOURVRS_GATEWAY_COORDINATOR_H
#define FOURVRS_GATEWAY_COORDINATOR_H

#include <signal.h>
#include "config/gateway_persistence.h"

#define GATEWAY_STARTUP_PROGRESS_TOTAL 14U
#define GATEWAY_STARTUP_EVENT_CAPACITY 16U
#define GATEWAY_COORDINATOR_SHUTDOWN_TIMEOUT 2000U
#define GATEWAY_NO_PORT 0xffffffffU

typedef enum gateway_application_state {
    GATEWAY_APP_BOOTSTRAP = 0,
    GATEWAY_APP_LOADING_CONFIGURATION,
    GATEWAY_APP_SELECTING_CONFIGURATION,
    GATEWAY_APP_VALIDATING_CONFIGURATION,
    GATEWAY_APP_INITIALIZING_CONTROLLER,
    GATEWAY_APP_STARTING_PORTS,
    GATEWAY_APP_CHECKING_HEALTH,
    GATEWAY_APP_READY,
    GATEWAY_APP_DEGRADED,
    GATEWAY_APP_SAFE_MODE,
    GATEWAY_APP_SHUTTING_DOWN,
    GATEWAY_APP_STOPPED,
    GATEWAY_APP_FATAL_ERROR
} gateway_application_state_t;

typedef enum gateway_application_error {
    GATEWAY_APP_ERROR_NONE = 0,
    GATEWAY_APP_ERROR_CONFIG_INVALID,
    GATEWAY_APP_ERROR_CONFIG_UNSUPPORTED,
    GATEWAY_APP_ERROR_STORAGE_IO,
    GATEWAY_APP_ERROR_BINDING_INIT,
    GATEWAY_APP_ERROR_CONTROLLER_INIT,
    GATEWAY_APP_ERROR_PORT_START,
    GATEWAY_APP_ERROR_SHUTDOWN_INCOMPLETE
} gateway_application_error_t;

typedef enum gateway_startup_event_result {
    GATEWAY_EVENT_BEGIN = 0,
    GATEWAY_EVENT_OK,
    GATEWAY_EVENT_RECOVERED,
    GATEWAY_EVENT_SKIPPED,
    GATEWAY_EVENT_FAILED,
    GATEWAY_EVENT_COMPLETE
} gateway_startup_event_result_t;

typedef struct gateway_startup_event {
    unsigned int sequence;
    gateway_application_state_t stage;
    gateway_startup_event_result_t result;
    unsigned int progress_numerator;
    unsigned int progress_denominator;
    unsigned int progress_percent;
    unsigned int port_index;
    gateway_application_error_t error;
} gateway_startup_event_t;

typedef struct gateway_coordinator_health {
    gateway_application_state_t state;
    gateway_config_source_t config_source;
    gateway_config_result_t persistence_result;
    gateway_application_error_t error;
    unsigned int progress_percent;
    unsigned int enabled_ports;
    unsigned int ready_ports;
    unsigned int error_ports;
    unsigned int disabled_ports;
    unsigned int clients;
    unsigned int accepted;
    unsigned int completed;
    unsigned int timeouts;
    unsigned int recoveries;
    unsigned int stale_responses;
    core_tick_t started_at;
    core_tick_t uptime;
    gateway_startup_event_t last_event;
    gateway_health_t gateway;
    unsigned int controller_initialized;
    unsigned int cleanup_complete;
} gateway_coordinator_health_t;

typedef gateway_config_result_t (*gateway_configuration_select_fn)(
    void *context, const char *directory, gateway_persistent_config_t *config,
    gateway_config_source_t *source);

typedef int (*gateway_binding_provider_fn)(
    void *context,
    const gateway_port_config_t ports[GATEWAY_PORT_COUNT],
    gateway_port_binding_t bindings[GATEWAY_PORT_COUNT]);
typedef int (*gateway_binding_attach_fn)(void *context,
                                        gateway_controller_t *controller);

typedef struct gateway_controller_driver {
    int (*init)(gateway_controller_t *controller,
                const gateway_port_config_t ports[GATEWAY_PORT_COUNT],
                const gateway_port_binding_t bindings[GATEWAY_PORT_COUNT]);
    void (*start)(gateway_controller_t *controller, core_tick_t now);
    void (*step)(gateway_controller_t *controller, core_tick_t now);
    void (*shutdown)(gateway_controller_t *controller, core_tick_t now);
    void (*health)(const gateway_controller_t *controller, gateway_health_t *health);
} gateway_controller_driver_t;

typedef struct gateway_run_control {
    volatile sig_atomic_t stop_requested;
} gateway_run_control_t;

typedef struct gateway_coordinator {
    gateway_controller_t controller;
    gateway_persistent_config_t selected;
    gateway_port_binding_t bindings[GATEWAY_PORT_COUNT];
    gateway_application_state_t state;
    gateway_application_error_t error;
    gateway_config_source_t config_source;
    gateway_config_result_t persistence_result;
    gateway_configuration_select_fn select_configuration;
    void *select_context;
    gateway_binding_provider_fn provide_bindings;
    gateway_binding_attach_fn attach_bindings;
    void *binding_context;
    const gateway_controller_driver_t *controller_driver;
    gateway_run_control_t *run_control;
    const char *configuration_directory;
    gateway_startup_event_t events[GATEWAY_STARTUP_EVENT_CAPACITY];
    unsigned int event_head;
    unsigned int event_count;
    unsigned int event_sequence;
    unsigned int progress_numerator;
    unsigned int startup_ports_complete;
    unsigned int controller_initialized;
    unsigned int controller_start_called;
    unsigned int controller_shutdown_called;
    unsigned int cleanup_complete;
    core_tick_t started_at;
    long shutdown_deadline;
} gateway_coordinator_t;

void gateway_run_control_init(gateway_run_control_t *control);
void gateway_run_control_request_stop(gateway_run_control_t *control);
const gateway_controller_driver_t *gateway_default_controller_driver(void);
int gateway_coordinator_init(gateway_coordinator_t *coordinator,
                             const char *configuration_directory,
                             gateway_run_control_t *run_control,
                             gateway_configuration_select_fn select_configuration,
                             void *select_context,
                             gateway_binding_provider_fn provide_bindings,
                             gateway_binding_attach_fn attach_bindings,
                             void *binding_context,
                             const gateway_controller_driver_t *controller_driver,
                             core_tick_t now);
void gateway_coordinator_step(gateway_coordinator_t *coordinator, core_tick_t now);
void gateway_coordinator_request_shutdown(gateway_coordinator_t *coordinator,
                                          core_tick_t now);
void gateway_coordinator_health(const gateway_coordinator_t *coordinator,
                                core_tick_t now,
                                gateway_coordinator_health_t *health);
unsigned int gateway_coordinator_event_count(const gateway_coordinator_t *coordinator);
int gateway_coordinator_event(const gateway_coordinator_t *coordinator,
                              unsigned int oldest_index,
                              gateway_startup_event_t *event);
unsigned int gateway_coordinator_memory_bytes(void);

#endif
