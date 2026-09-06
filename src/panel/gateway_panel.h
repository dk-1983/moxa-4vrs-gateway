#ifndef FOURVRS_GATEWAY_PANEL_H
#define FOURVRS_GATEWAY_PANEL_H

#include "app/gateway_application.h"

#define GATEWAY_PANEL_COLUMNS 16U
#define GATEWAY_PANEL_ROWS 8U
#define GATEWAY_PANEL_MENU_VISIBLE 5U
#define GATEWAY_PANEL_FIELD_COUNT 13U

typedef struct gateway_panel_screen {
    char row[GATEWAY_PANEL_ROWS][GATEWAY_PANEL_COLUMNS + 1U];
} gateway_panel_screen_t;

typedef struct gateway_panel_ops {
    int (*display_open)(void *context);
    int (*keypad_open)(void *context);
    int (*display_draw)(void *context, const gateway_panel_screen_t *screen);
    int (*keypad_poll)(void *context, unsigned int *raw_key);
    void (*display_close)(void *context);
    void (*keypad_close)(void *context);
} gateway_panel_ops_t;

typedef enum gateway_panel_view {
    GATEWAY_PANEL_STARTUP = 0,
    GATEWAY_PANEL_HOME,
    GATEWAY_PANEL_HELP,
    GATEWAY_PANEL_MENU,
    GATEWAY_PANEL_STATUS,
    GATEWAY_PANEL_PORTS,
    GATEWAY_PANEL_PORT_DETAIL,
    GATEWAY_PANEL_CONFIG_PORTS,
    GATEWAY_PANEL_CONFIG_FIELDS,
    GATEWAY_PANEL_EDIT_FIELD,
    GATEWAY_PANEL_APPLY_CONFIRM,
    GATEWAY_PANEL_APPLY_RESULT,
    GATEWAY_PANEL_DIAGNOSTICS,
    GATEWAY_PANEL_STARTUP_EVENTS,
    GATEWAY_PANEL_SYSTEM,
    GATEWAY_PANEL_TIME_EDIT,
    GATEWAY_PANEL_TIME_CONFIRM,
    GATEWAY_PANEL_TIME_RESULT,
    GATEWAY_PANEL_NTP_SETTINGS,
    GATEWAY_PANEL_NTP_EDIT,
    GATEWAY_PANEL_NTP_CONFIRM,
    GATEWAY_PANEL_NTP_RESULT,
    GATEWAY_PANEL_SHUTDOWN_CONFIRM,
    GATEWAY_PANEL_ABOUT,
    GATEWAY_PANEL_NETWORK,
    GATEWAY_PANEL_NETWORK_EDIT,
    GATEWAY_PANEL_NETWORK_CONFIRM,
    GATEWAY_PANEL_NETWORK_RESULT,
    GATEWAY_PANEL_DISPLAY,
    GATEWAY_PANEL_BACKLIGHT
} gateway_panel_view_t;

typedef enum gateway_panel_key {
    GATEWAY_PANEL_KEY_F1 = 0,
    GATEWAY_PANEL_KEY_F2 = 1,
    GATEWAY_PANEL_KEY_F3 = 2,
    GATEWAY_PANEL_KEY_F4 = 3,
    GATEWAY_PANEL_KEY_F5 = 4
} gateway_panel_key_t;

typedef struct gateway_panel_health {
    unsigned int display_available;
    unsigned int keypad_available;
    unsigned int render_count;
    unsigned int suppressed_render_count;
    unsigned int key_event_count;
    unsigned int display_errors;
    unsigned int keypad_errors;
    gateway_panel_view_t view;
    int last_display_error;
    int last_keypad_error;
} gateway_panel_health_t;

typedef struct gateway_panel {
    unsigned int backlight_choice;
    gateway_network_settings_t lan2_candidate;
    unsigned int lan2_cursor, network_page;
    const gateway_panel_ops_t *ops;
    void *ops_context;
    gateway_panel_screen_t displayed;
    gateway_panel_screen_t next;
    gateway_persistent_config_t candidate;
    gateway_port_config_t edit_original;
    gateway_panel_health_t health;
    gateway_panel_view_t view;
    unsigned int selected;
    unsigned int scroll;
    unsigned int port_index;
    unsigned int detail_page;
    unsigned int help_page;
    unsigned int field_index;
    unsigned int edit_octet;
    unsigned int startup_event_page;
    gateway_system_time_t displayed_time;
    gateway_system_time_t time_candidate;
    gateway_system_time_result_t time_result;
    gateway_product_settings_t settings_original;
    unsigned int ntp_field;
    unsigned int ntp_cursor;
    core_tick_t time_refresh_at;
    unsigned int system_page;
    unsigned int time_field;
    unsigned int have_displayed_time;
    unsigned int initialized;
    unsigned int have_displayed;
    unsigned int candidate_loaded;
    unsigned int network_change;
    unsigned int local_validation_failed;
    unsigned int dirty;
} gateway_panel_t;

int gateway_panel_init(gateway_panel_t *panel, const gateway_panel_ops_t *ops,
                       void *ops_context);
void gateway_panel_step(gateway_panel_t *panel, gateway_application_t *application);
void gateway_panel_shutdown(gateway_panel_t *panel);
void gateway_panel_health(const gateway_panel_t *panel,
                          gateway_panel_health_t *health);
unsigned int gateway_panel_memory_bytes(void);
unsigned int gateway_panel_screen_bytes(void);

#endif
