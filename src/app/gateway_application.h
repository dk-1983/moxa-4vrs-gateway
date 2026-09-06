#ifndef FOURVRS_GATEWAY_APPLICATION_H
#define FOURVRS_GATEWAY_APPLICATION_H

#include "gateway/gateway_coordinator.h"
#include "gateway/gateway_real_adapter.h"
#include "version.h"
#include "network/gateway_network_runtime.h"

#define GATEWAY_APPLICATION_WAIT_USEC 1000U
#define GATEWAY_PLATFORM_TEXT_MAX 64U
#define GATEWAY_SYSTEM_TIME_YEAR_MIN 2000U
#define GATEWAY_SYSTEM_TIME_YEAR_MAX 2037U
#define GATEWAY_NTP_ATTEMPT_TIMEOUT_MS 15000UL
#define GATEWAY_NTP_RETRY_INITIAL_MS 5000UL
#define GATEWAY_NTP_RETRY_SECOND_MS 30000UL
#define GATEWAY_NTP_RETRY_MAX_MS 300000UL
#define GATEWAY_NTP_RESYNC_MS 3600000UL

typedef struct gateway_application gateway_application_t;

typedef enum gateway_configuration_transaction_state {
    GATEWAY_CONFIG_TX_IDLE = 0,
    GATEWAY_CONFIG_TX_ACTIVATING,
    GATEWAY_CONFIG_TX_PROMOTING,
    GATEWAY_CONFIG_TX_ROLLING_BACK,
    GATEWAY_CONFIG_TX_SUCCEEDED,
    GATEWAY_CONFIG_TX_UNCHANGED,
    GATEWAY_CONFIG_TX_FAILED,
    GATEWAY_CONFIG_TX_DURABILITY_UNCERTAIN
} gateway_configuration_transaction_state_t;

typedef gateway_config_result_t (*gateway_configuration_stage_fn)(
    void *context, const char *directory,
    const gateway_persistent_config_t *config);
typedef gateway_config_result_t (*gateway_configuration_promote_fn)(
    void *context, const char *directory);
typedef void (*gateway_configuration_discard_fn)(void *context,
                                                  const char *directory);

typedef enum gateway_process_state {
    GATEWAY_PROCESS_NEW = 0,
    GATEWAY_PROCESS_STARTING,
    GATEWAY_PROCESS_RUNNING,
    GATEWAY_PROCESS_STOPPING,
    GATEWAY_PROCESS_STOPPED
} gateway_process_state_t;

typedef enum gateway_process_exit {
    GATEWAY_EXIT_CLEAN = 0,
    GATEWAY_EXIT_SAFE_MODE = 10,
    GATEWAY_EXIT_FATAL_STARTUP = 20,
    GATEWAY_EXIT_CLEANUP_FAILURE = 21,
    GATEWAY_EXIT_INVALID_INVOCATION = 22
} gateway_process_exit_t;

typedef struct gateway_product_metadata {
    const char *name;
    const char *version;
} gateway_product_metadata_t;

typedef struct gateway_platform_info {
    char model[GATEWAY_PLATFORM_TEXT_MAX];
    char kernel[GATEWAY_PLATFORM_TEXT_MAX];
    char architecture[GATEWAY_PLATFORM_TEXT_MAX];
    unsigned int word_bits;
    unsigned int big_endian;
} gateway_platform_info_t;

typedef struct gateway_system_time {
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
} gateway_system_time_t;

typedef enum gateway_system_time_result {
    GATEWAY_SYSTEM_TIME_OK = 0,
    GATEWAY_SYSTEM_TIME_INVALID,
    GATEWAY_SYSTEM_TIME_READ_FAILED,
    GATEWAY_SYSTEM_TIME_SET_FAILED,
    GATEWAY_SYSTEM_TIME_RTC_SYNC_FAILED,
    GATEWAY_SYSTEM_TIME_RTC_UNSUPPORTED
} gateway_system_time_result_t;

typedef enum gateway_time_trust_state {
    GATEWAY_TIME_UNKNOWN = 0,
    GATEWAY_TIME_UNSYNCED,
    GATEWAY_TIME_SYNCING,
    GATEWAY_TIME_SYNCED,
    GATEWAY_TIME_MANUAL,
    GATEWAY_TIME_ERROR,
    GATEWAY_TIME_HOLDOVER
} gateway_time_trust_state_t;

/* A readable RTC is not evidence of a healthy backup battery. */
typedef enum gateway_rtc_state {
    GATEWAY_RTC_UNKNOWN = 0,
    GATEWAY_RTC_UNAVAILABLE,
    GATEWAY_RTC_INVALID,
    GATEWAY_RTC_READ_FAILED,
    GATEWAY_RTC_UNVERIFIED,
    GATEWAY_RTC_SAVED,
    GATEWAY_RTC_WRITE_FAILED
} gateway_rtc_state_t;

typedef enum gateway_ntp_result {
    GATEWAY_NTP_NOT_CONFIGURED = 0,
    GATEWAY_NTP_NOT_ATTEMPTED,
    GATEWAY_NTP_IN_PROGRESS,
    GATEWAY_NTP_SUCCEEDED,
    GATEWAY_NTP_FAILED,
    GATEWAY_NTP_TIMED_OUT,
    GATEWAY_NTP_UNSUITABLE
} gateway_ntp_result_t;

typedef struct gateway_time_health {
    gateway_time_trust_state_t trust;
    gateway_ntp_result_t last_ntp_result;
    unsigned int ntp_enabled;
    unsigned int ntp_interval_hours;
    unsigned int ntp_test_active;
    unsigned int ntp_test_attempts;
    char ntp_server[GATEWAY_NTP_SERVER_MAX];
    unsigned int ntp_attempts;
    core_tick_t last_ntp_attempt;
    core_tick_t last_sync;
    core_tick_t next_ntp_attempt;
    unsigned int ntp_network_deferred;
    gateway_rtc_state_t rtc;
    core_tick_t last_rtc_save;
    unsigned int rtc_save_failures;
    unsigned int ntp_failures;
} gateway_time_health_t;

typedef int (*gateway_platform_provider_fn)(void *context,
                                            gateway_platform_info_t *info);
typedef core_tick_t (*gateway_application_clock_fn)(void *context);
typedef void (*gateway_application_wait_fn)(void *context,
                                            unsigned int microseconds);
typedef gateway_system_time_result_t (*gateway_system_time_get_fn)(
    void *context, gateway_system_time_t *value);
typedef gateway_system_time_result_t (*gateway_system_time_set_fn)(
    void *context, const gateway_system_time_t *value);
typedef int (*gateway_ntp_start_fn)(void *context, const char *server);
typedef int (*gateway_ntp_poll_fn)(void *context);
typedef void (*gateway_ntp_cancel_fn)(void *context);
typedef gateway_rtc_state_t (*gateway_rtc_operation_fn)(void *context);

typedef struct gateway_application_dependencies {
    int (*backlight_set)(void *context, unsigned int on);
    void *backlight_context;
    const gateway_network_environment_t *network_environment;
    const char *configuration_directory;
    gateway_configuration_select_fn select_configuration;
    void *select_context;
    gateway_binding_provider_fn provide_bindings;
    gateway_binding_attach_fn attach_bindings;
    void *binding_context;
    const gateway_controller_driver_t *controller_driver;
    gateway_platform_provider_fn platform_provider;
    void *platform_context;
    gateway_application_clock_fn clock;
    void *clock_context;
    gateway_application_wait_fn wait;
    void *wait_context;
    gateway_system_time_get_fn system_time_get;
    gateway_system_time_set_fn system_time_set;
    void *system_time_context;
    const char *ntp_server;
    gateway_ntp_start_fn ntp_start;
    gateway_ntp_poll_fn ntp_poll;
    gateway_ntp_cancel_fn ntp_cancel;
    void *ntp_context;
    gateway_rtc_operation_fn rtc_probe;
    gateway_rtc_operation_fn rtc_save;
    void *rtc_context;
    gateway_configuration_stage_fn stage_configuration;
    gateway_configuration_promote_fn promote_configuration;
    gateway_configuration_discard_fn discard_configuration;
    void *configuration_write_context;
} gateway_application_dependencies_t;

typedef struct gateway_configuration_transaction {
    gateway_persistent_config_t candidate;
    gateway_persistent_config_t previous;
    gateway_configuration_transaction_state_t state;
    gateway_config_result_t persistence_result;
    gateway_error_t validation_error;
    unsigned int port_index;
    unsigned int waiting;
    unsigned int rollback_failed;
} gateway_configuration_transaction_t;

typedef struct gateway_backlight_health {
    unsigned int desired_on, command_on, command_known, attempted;
    int last_error;
} gateway_backlight_health_t;

typedef struct gateway_application_health {
    gateway_backlight_health_t backlight;
    gateway_process_state_t process_state;
    gateway_process_exit_t exit_status;
    gateway_product_metadata_t product;
    gateway_platform_info_t platform;
    char configuration_directory[GATEWAY_CONFIG_PATH_MAX];
    gateway_coordinator_health_t coordinator;
    unsigned int shutdown_requests;
    unsigned int cleanup_attempts;
    gateway_time_health_t time;
} gateway_application_health_t;

struct gateway_application {
    gateway_backlight_health_t backlight;
    gateway_network_runtime_t network;
    gateway_coordinator_t coordinator;
    gateway_real_adapter_t adapters[GATEWAY_PORT_COUNT];
    gateway_run_control_t run_control;
    gateway_application_dependencies_t dependencies;
    gateway_platform_info_t platform;
    gateway_process_state_t process_state;
    gateway_process_exit_t exit_status;
    unsigned int prepared_adapters;
    unsigned int shutdown_requests;
    unsigned int cleanup_attempts;
    unsigned int fatal_seen;
    unsigned int safe_mode_seen;
    gateway_configuration_transaction_t configuration_transaction;
    gateway_time_health_t time;
    gateway_time_trust_state_t trust_before_sync;
    int ntp_helper_pid;
    int ntp_output_fd;
    char ntp_output[256];
    unsigned int ntp_output_used;
    unsigned int ntp_terminating;
    unsigned int time_settings_applied;
    unsigned int rtc_checked;
    core_deadline_t ntp_deadline;
    core_deadline_t ntp_retry_deadline;
    core_deadline_t ntp_normal_deadline;
};

const gateway_product_metadata_t *gateway_product_metadata(void);
int gateway_application_ntp_test(gateway_application_t *application, unsigned int enabled);
const char *gateway_rtc_state_name(gateway_rtc_state_t state);
int gateway_platform_info_default(void *context, gateway_platform_info_t *info);
core_tick_t gateway_application_monotonic_ms(void *context);
int gateway_system_time_validate(const gateway_system_time_t *value);
gateway_system_time_result_t gateway_system_time_get_default(
    void *context, gateway_system_time_t *value);
gateway_system_time_result_t gateway_system_time_set_default(
    void *context, const gateway_system_time_t *value);
gateway_system_time_result_t gateway_application_system_time_get(
    gateway_application_t *application, gateway_system_time_t *value);
gateway_system_time_result_t gateway_application_system_time_set(
    gateway_application_t *application, const gateway_system_time_t *value);
void gateway_application_time_health(const gateway_application_t *application,
                                     gateway_time_health_t *health);
void gateway_application_wait_default(void *context, unsigned int microseconds);
void gateway_application_dependencies_default(gateway_application_dependencies_t *deps);
void gateway_application_dependencies_production(
    gateway_application_dependencies_t *deps,
    gateway_application_t *application,
    const char *configuration_directory);
int gateway_application_init(gateway_application_t *application,
                             const gateway_application_dependencies_t *deps);
int gateway_application_init_production(gateway_application_t *application,
                                        const char *configuration_directory);
void gateway_application_step(gateway_application_t *application);
void gateway_application_request_stop(gateway_application_t *application);
int gateway_application_finished(const gateway_application_t *application);
gateway_process_exit_t gateway_application_exit_status(
    const gateway_application_t *application);
void gateway_application_health(const gateway_application_t *application,
                                gateway_application_health_t *health);
int gateway_application_request_configuration(
    gateway_application_t *application,
    const gateway_persistent_config_t *candidate);
gateway_configuration_transaction_state_t
gateway_application_configuration_state(const gateway_application_t *application);
gateway_config_result_t gateway_application_configuration_result(
    const gateway_application_t *application);
unsigned int gateway_application_memory_bytes(void);
unsigned int gateway_application_adapter_bytes(void);
unsigned int gateway_application_max_fds(void);
int gateway_application_network_apply(gateway_application_t *, const gateway_network_settings_t *);
/* Saves through the common configuration transaction. No physical readback. */
int gateway_application_backlight_set(gateway_application_t *, unsigned int on);
int gateway_application_network_keep(gateway_application_t *);
int gateway_application_network_revert(gateway_application_t *);
int gateway_application_network_boot(const gateway_network_environment_t *);
void gateway_application_network_step(gateway_application_t *, core_tick_t);
void gateway_application_network_map(gateway_application_t *, gateway_persistent_config_t *, int reverse);

#endif
