#ifndef FOURVRS_GATEWAY_PERSISTENCE_H
#define FOURVRS_GATEWAY_PERSISTENCE_H

#include <stddef.h>
#include "gateway/gateway_controller.h"

#define GATEWAY_CONFIG_SCHEMA_VERSION 1U
#define GATEWAY_CONFIG_WEB_SCHEMA_VERSION 2U
#define GATEWAY_CONFIG_PROTOCOL_SCHEMA_VERSION 3U
#define GATEWAY_CONFIG_MAX_BYTES 4096U
#define GATEWAY_CONFIG_PATH_MAX 256U
#define GATEWAY_CONFIG_ACTIVE_NAME "gateway.conf"
#define GATEWAY_CONFIG_BACKUP_NAME "gateway.conf.good"
#define GATEWAY_CONFIG_STAGED_NAME "gateway.conf.tmp"
#define GATEWAY_CONFIG_TARGET_DIRECTORY "/var/hda/4vrs/config"
#define GATEWAY_NTP_SERVER_MAX 64U

typedef struct gateway_product_settings {
    unsigned int web_protocol; /* 0=HTTP, 1=HTTPS (TLS); legacy files decode as HTTPS */
    unsigned int web_enabled;
    unsigned int web_interface; /* 0=LAN1/eth0, 1=LAN2/eth1, 2=Both */
    unsigned int backlight_on;
    int ntp_enabled;
    unsigned int ntp_interval_hours;
    char ntp_server[GATEWAY_NTP_SERVER_MAX];
} gateway_product_settings_t;

typedef struct gateway_persistent_config {
    unsigned int schema_version;
    gateway_product_settings_t settings;
    gateway_port_config_t ports[GATEWAY_PORT_COUNT];
} gateway_persistent_config_t;

typedef enum gateway_config_result {
    GATEWAY_CONFIG_OK = 0,
    GATEWAY_CONFIG_ABSENT,
    GATEWAY_CONFIG_INVALID,
    GATEWAY_CONFIG_UNSUPPORTED_VERSION,
    GATEWAY_CONFIG_IO_ERROR,
    GATEWAY_CONFIG_UNCHANGED,
    GATEWAY_CONFIG_DURABILITY_UNCERTAIN
} gateway_config_result_t;

typedef enum gateway_config_source {
    GATEWAY_CONFIG_SOURCE_ACTIVE = 0,
    GATEWAY_CONFIG_SOURCE_BACKUP,
    GATEWAY_CONFIG_SOURCE_DEFAULTS,
    GATEWAY_CONFIG_SOURCE_SAFE_MODE
} gateway_config_source_t;

typedef enum gateway_save_failpoint {
    GATEWAY_SAVE_FAIL_NONE = 0,
    GATEWAY_SAVE_FAIL_WRITE,
    GATEWAY_SAVE_FAIL_FLUSH,
    GATEWAY_SAVE_FAIL_FILE_SYNC,
    GATEWAY_SAVE_FAIL_BACKUP_RENAME,
    GATEWAY_SAVE_FAIL_PROMOTE_RENAME,
    GATEWAY_SAVE_FAIL_DIRECTORY_SYNC
} gateway_save_failpoint_t;

void gateway_persistent_defaults(gateway_persistent_config_t *config);
void gateway_persistent_safe_mode(gateway_persistent_config_t *config);
int gateway_product_settings_validate(const gateway_product_settings_t *settings);
gateway_config_result_t gateway_config_encode(const gateway_persistent_config_t *config,
                                              char *output, size_t capacity, size_t *length);
gateway_config_result_t gateway_config_decode(const char *input, size_t length,
                                              gateway_persistent_config_t *config);
gateway_config_result_t gateway_config_load_file(const char *path,
                                                 gateway_persistent_config_t *config);
gateway_config_result_t gateway_config_startup_select(const char *directory,
                                                      gateway_persistent_config_t *config,
                                                      gateway_config_source_t *source);
gateway_config_result_t gateway_config_save(const char *directory,
                                            const gateway_persistent_config_t *config,
                                            gateway_save_failpoint_t failpoint);
gateway_config_result_t gateway_config_stage(const char *directory,
                                             const gateway_persistent_config_t *config,
                                             gateway_save_failpoint_t failpoint);
gateway_config_result_t gateway_config_promote(const char *directory,
                                               gateway_save_failpoint_t failpoint);
void gateway_config_discard_staged(const char *directory);
unsigned int gateway_persistent_config_memory_bytes(void);

#endif
