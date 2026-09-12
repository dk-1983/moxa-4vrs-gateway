#define _BSD_SOURCE 1

#include "core/platform.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/times.h>
#include <unistd.h>

#include <moxadevice.h>

#include "config/config_model.h"
#include "status/system_status.h"
#include "ui/keypad_device.h"
#include "ui/lcm_device.h"
#include "ui/ui_defaults.h"
#include "version.h"

typedef enum page_id {
    PAGE_HOME, PAGE_ROOT, PAGE_HELP, PAGE_SYSTEM, PAGE_NETWORK, PAGE_PORTS,
    PAGE_PORT, PAGE_SERIAL, PAGE_VALUE, PAGE_TRANSPORT, PAGE_ENDPOINT,
    PAGE_ABOUT, PAGE_INFO, PAGE_EDIT
} page_id_t;

typedef enum value_kind {
    VALUE_MODE, VALUE_BAUD, VALUE_DATA, VALUE_PARITY, VALUE_STOP,
    VALUE_TRANSPORT, VALUE_NETWORK_PORT
} value_kind_t;

typedef struct ui_state {
    page_id_t page;
    page_id_t return_page;
    int selected;
    int help_page;
    int port_index;
    int port_scroll;
    value_kind_t value_kind;
    int edit_original;
    int edit_staged;
    configuration_t config;
    home_status_t home;
} ui_state_t;

static clock_t now_ticks(void)
{
    struct tms ignored;
    return times(&ignored);
}

static int expired(clock_t since, long seconds, long ticks_per_second)
{
    return now_ticks() - since >= (clock_t)(seconds * ticks_per_second);
}

static void row_format(char *buffer, char marker, const char *label)
{
    buffer[0] = marker;
    buffer[1] = ' ';
    strncpy(buffer + 2, label, UI_COLUMNS - 2);
    buffer[UI_COLUMNS] = '\0';
}

static void render_fixed_menu(ui_screen_t *screen, const char *title,
                              const char *items[], int count, int selected)
{
    char line[UI_COLUMNS + 1];
    int index;
    ui_screen_clear(screen);
    ui_screen_set(screen, 0, title);
    for (index = 0; index < count && index < 5; ++index) {
        row_format(line, index == selected ? '>' : ' ', items[index]);
        ui_screen_set(screen, index + 1, line);
    }
}

static void render_home(ui_state_t *state, ui_screen_t *screen)
{
    home_status_read(&state->home);
    ui_screen_clear(screen);
    ui_screen_set(screen, 0, "4VRS CONFIG");
    ui_screen_set(screen, 1, FOURVRS_VERSION);
    ui_screen_set(screen, 2, "" FOURVRS_LAN_PREFIX "0");
    ui_screen_set(screen, 3, state->home.eth0_ipv4);
    ui_screen_set(screen, 4, "" FOURVRS_LAN_PREFIX "1");
    ui_screen_set(screen, 5, state->home.eth1_ipv4);
    ui_screen_set(screen, 7, state->home.wall_clock_trusted ? "TIME VALID" : "TIME UNSYNCED");
    printf("home_" FOURVRS_LAN_PREFIX "0=%s home_" FOURVRS_LAN_PREFIX "1=%s time=%s\n", state->home.eth0_ipv4,
           state->home.eth1_ipv4,
           state->home.wall_clock_trusted ? "TRUSTED" : "UNSYNCED");
}

static void render_root(ui_state_t *state, ui_screen_t *screen)
{
    static const char *items[] = { "System", "Network", "Ports", "About" };
    char line[UI_COLUMNS + 1];
    int index;
    ui_screen_clear(screen);
    ui_screen_set(screen, 0, FOURVRS_VERSION);
    for (index = 0; index < 4; ++index) {
        row_format(line, index == state->selected ? '>' : ' ', items[index]);
        ui_screen_set(screen, index + 2, line);
    }
}

static void render_help(ui_state_t *state, ui_screen_t *screen)
{
    ui_screen_clear(screen);
    ui_screen_set(screen, 0, "Help");
    if (state->help_page == 0) {
        ui_screen_set(screen, 1, "F1 Help / Back");
        ui_screen_set(screen, 2, "F2 Up");
        ui_screen_set(screen, 3, "F3 Select / OK");
        ui_screen_set(screen, 4, "F4 Down");
        ui_screen_set(screen, 5, "F5 Action");
        ui_screen_set(screen, 6, "Page 1/2");
    } else {
        ui_screen_set(screen, 1, "Browse: F2/F4");
        ui_screen_set(screen, 2, "Choose: F3");
        ui_screen_set(screen, 3, "Edit: F5");
        ui_screen_set(screen, 4, "Return: F1");
        ui_screen_set(screen, 6, "Page 2/2");
    }
}

static void render_ports(ui_state_t *state, ui_screen_t *screen)
{
    char line[UI_COLUMNS + 1];
    char status[UI_COLUMNS + 1];
    int row;
    int index;
    ui_screen_clear(screen);
    ui_screen_set(screen, 0, "Ports");
    if (state->selected < state->port_scroll)
        state->port_scroll = state->selected;
    if (state->selected >= state->port_scroll + 5)
        state->port_scroll = state->selected - 4;
    for (row = 0; row < 5; ++row) {
        index = state->port_scroll + row;
        if (index < FOURVRS_PORT_COUNT) {
            row_format(line, index == state->selected ? '>' : ' ',
                       config_port_name(index));
            ui_screen_set(screen, row + 1, line);
        }
    }
    sprintf(status, "%d-%d / 8", state->port_scroll + 1,
            state->port_scroll + 5 > 8 ? 8 : state->port_scroll + 5);
    ui_screen_set(screen, 6, status);
}

static void value_text(const ui_state_t *state, int value, char *buffer)
{
    switch (state->value_kind) {
    case VALUE_MODE:
        strcpy(buffer, serial_mode_label((serial_mode_t)value));
        break;
    case VALUE_BAUD:
        sprintf(buffer, "%lu", baud_value(value));
        break;
    case VALUE_DATA:
    case VALUE_STOP:
    case VALUE_NETWORK_PORT:
        sprintf(buffer, "%d", value);
        break;
    case VALUE_PARITY:
        strcpy(buffer, parity_label((parity_mode_t)value));
        break;
    default:
        strcpy(buffer, transport_label((transport_type_t)value));
        break;
    }
}

static const char *value_title(value_kind_t kind)
{
    static const char *titles[] = {
        "Mode", "Baud", "Data bits", "Parity", "Stop bits",
        "Transport", "Network port"
    };
    return titles[(int)kind];
}

static int current_value(const ui_state_t *state)
{
    const port_config_t *port = &state->config.port[state->port_index];
    switch (state->value_kind) {
    case VALUE_MODE: return port->mode;
    case VALUE_BAUD: return port->baud_index;
    case VALUE_DATA: return port->data_bits;
    case VALUE_PARITY: return port->parity;
    case VALUE_STOP: return port->stop_bits;
    case VALUE_TRANSPORT: return port->transport;
    default: return port->network_port;
    }
}

static void set_current_value(ui_state_t *state, int value)
{
    port_config_t *port = &state->config.port[state->port_index];
    switch (state->value_kind) {
    case VALUE_MODE: port->mode = (serial_mode_t)value; break;
    case VALUE_BAUD: port->baud_index = value; break;
    case VALUE_DATA: port->data_bits = value; break;
    case VALUE_PARITY: port->parity = (parity_mode_t)value; break;
    case VALUE_STOP: port->stop_bits = value; break;
    case VALUE_TRANSPORT: port->transport = (transport_type_t)value; break;
    default:
        if (network_port_valid(value))
            port->network_port = value;
        break;
    }
}

static void render_value(ui_state_t *state, ui_screen_t *screen, int editing)
{
    char value[UI_COLUMNS + 1];
    ui_screen_clear(screen);
    ui_screen_set(screen, 0, config_port_name(state->port_index));
    ui_screen_set(screen, 1, value_title(state->value_kind));
    value_text(state, editing ? state->edit_staged : current_value(state), value);
    ui_screen_set(screen, 3, value);
    ui_screen_set(screen, 5, editing ? "STAGED ONLY" : "CURRENT");
    ui_screen_set(screen, 6, editing ? "F3 OK F1 CANCEL" : "F5 EDIT");
}

static void render_info(ui_state_t *state, ui_screen_t *screen)
{
    ui_screen_clear(screen);
    if (state->return_page == PAGE_PORT) {
        ui_screen_set(screen, 0, config_port_name(state->port_index));
        ui_screen_set(screen, 1, "Status");
        ui_screen_set(screen, 3, config_device_name(state->port_index));
        ui_screen_set(screen, 4, "MODEL ONLY");
        ui_screen_set(screen, 6, "Hardware: unused");
    } else if (state->return_page == PAGE_SYSTEM) {
        static const char *titles[] = { "Display", "Time", "Device Info", "Resources" };
        ui_screen_set(screen, 0, titles[state->selected]);
        if (state->selected == 0) {
            ui_screen_set(screen, 2, "Backlight: ON");
            ui_screen_set(screen, 4, "No bright/contrast");
        } else if (state->selected == 1) {
            ui_screen_set(screen, 2, "TIME UNSYNCED");
            ui_screen_set(screen, 4, "RTC untrusted");
        } else {
            ui_screen_set(screen, 2, "READ-ONLY");
            ui_screen_set(screen, 4, "Future details");
        }
    } else {
        static const char *titles[] = { "Interfaces", "Routes", "DNS", "Time Sync" };
        ui_screen_set(screen, 0, titles[state->selected]);
        if (state->selected == 0) {
            home_status_read(&state->home);
            ui_screen_set(screen, 1, "" FOURVRS_LAN_PREFIX "0");
            ui_screen_set(screen, 2, state->home.eth0_ipv4);
            ui_screen_set(screen, 3, "" FOURVRS_LAN_PREFIX "1");
            ui_screen_set(screen, 4, state->home.eth1_ipv4);
        } else if (state->selected == 3) {
            ui_screen_set(screen, 2, "TIME UNSYNCED");
            ui_screen_set(screen, 4, "Config: future");
        } else {
            ui_screen_set(screen, 2, "READ-ONLY");
            ui_screen_set(screen, 4, "Future details");
        }
    }
}

static void render(ui_state_t *state, ui_screen_t *screen)
{
    static const char *system_items[] = { "Display", "Time", "Device Info", "Resources" };
    static const char *network_items[] = { "Interfaces", "Routes", "DNS", "Time Sync" };
    static const char *port_items[] = { "Status", "Serial", "Transport", "Network" };
    static const char *serial_items[] = { "Mode", "Baud", "Data", "Parity", "Stop" };

    switch (state->page) {
    case PAGE_HOME: render_home(state, screen); break;
    case PAGE_ROOT: render_root(state, screen); break;
    case PAGE_HELP: render_help(state, screen); break;
    case PAGE_SYSTEM:
        render_fixed_menu(screen, "System", system_items, 4, state->selected); break;
    case PAGE_NETWORK:
        render_fixed_menu(screen, "Network", network_items, 4, state->selected); break;
    case PAGE_PORTS: render_ports(state, screen); break;
    case PAGE_PORT:
        render_fixed_menu(screen, config_port_name(state->port_index), port_items, 4,
                          state->selected); break;
    case PAGE_SERIAL:
        render_fixed_menu(screen, "Serial", serial_items, 5, state->selected); break;
    case PAGE_VALUE: render_value(state, screen, 0); break;
    case PAGE_TRANSPORT:
        state->value_kind = VALUE_TRANSPORT; render_value(state, screen, 0); break;
    case PAGE_ENDPOINT:
        state->value_kind = VALUE_NETWORK_PORT; render_value(state, screen, 0); break;
    case PAGE_EDIT: render_value(state, screen, 1); break;
    case PAGE_ABOUT:
        ui_screen_clear(screen);
        ui_screen_set(screen, 0, "4VRS CONFIG");
        ui_screen_set(screen, 1, FOURVRS_VERSION);
        ui_screen_set(screen, 3, "UI PROTOTYPE");
        break;
    default: render_info(state, screen); break;
    }
}

static void move_selection(ui_state_t *state, int direction)
{
    int limit = 4;
    if (state->page == PAGE_PORTS)
        limit = FOURVRS_PORT_COUNT;
    else if (state->page == PAGE_SERIAL)
        limit = 5;
    state->selected += direction;
    if (state->selected < 0)
        state->selected = limit - 1;
    if (state->selected >= limit)
        state->selected = 0;
}

static void change_staged(ui_state_t *state, int direction)
{
    int limit;
    int minimum = 0;
    switch (state->value_kind) {
    case VALUE_MODE: limit = SERIAL_MODE_COUNT; break;
    case VALUE_BAUD: limit = FOURVRS_BAUD_COUNT; break;
    case VALUE_DATA: minimum = 5; limit = 9; break;
    case VALUE_PARITY: limit = PARITY_COUNT; break;
    case VALUE_STOP: minimum = 1; limit = 3; break;
    case VALUE_TRANSPORT: limit = TRANSPORT_COUNT; break;
    default:
        state->edit_staged += direction;
        if (!network_port_valid(state->edit_staged))
            state->edit_staged = direction > 0 ? 1 : 65535;
        return;
    }
    state->edit_staged += direction;
    if (state->edit_staged < minimum)
        state->edit_staged = limit - 1;
    if (state->edit_staged >= limit)
        state->edit_staged = minimum;
}

static void select_item(ui_state_t *state)
{
    if (state->page == PAGE_ROOT) {
        state->page = state->selected == 0 ? PAGE_SYSTEM :
                      state->selected == 1 ? PAGE_NETWORK :
                      state->selected == 2 ? PAGE_PORTS : PAGE_ABOUT;
        state->selected = 0;
    } else if (state->page == PAGE_SYSTEM || state->page == PAGE_NETWORK) {
        state->return_page = state->page;
        state->page = PAGE_INFO;
    } else if (state->page == PAGE_PORTS) {
        state->port_index = state->selected;
        state->selected = 0;
        state->page = PAGE_PORT;
    } else if (state->page == PAGE_PORT) {
        if (state->selected == 1) {
            state->selected = 0;
            state->page = PAGE_SERIAL;
        } else if (state->selected == 2) {
            state->page = PAGE_TRANSPORT;
        } else if (state->selected == 3) {
            state->page = PAGE_ENDPOINT;
        } else {
            state->return_page = PAGE_PORT;
            state->page = PAGE_INFO;
        }
    } else if (state->page == PAGE_SERIAL) {
        state->value_kind = (value_kind_t)state->selected;
        state->page = PAGE_VALUE;
    }
}

static void go_back(ui_state_t *state)
{
    if (state->page == PAGE_ROOT) {
        state->page = PAGE_HELP;
        state->help_page = 0;
    } else if (state->page == PAGE_HELP || state->page == PAGE_SYSTEM ||
               state->page == PAGE_NETWORK || state->page == PAGE_PORTS ||
               state->page == PAGE_ABOUT) {
        state->page = PAGE_ROOT;
        state->selected = 0;
    } else if (state->page == PAGE_PORT) {
        state->page = PAGE_PORTS;
        state->selected = state->port_index;
    } else if (state->page == PAGE_SERIAL || state->page == PAGE_TRANSPORT ||
               state->page == PAGE_ENDPOINT) {
        state->page = PAGE_PORT;
        state->selected = 0;
    } else if (state->page == PAGE_VALUE) {
        state->page = PAGE_SERIAL;
        state->selected = (int)state->value_kind;
    } else if (state->page == PAGE_INFO) {
        state->page = state->return_page;
    } else if (state->page == PAGE_EDIT) {
        state->edit_staged = state->edit_original;
        state->page = state->return_page;
        printf("edit=CANCEL restored=%d\n", state->edit_original);
    }
}

static void start_edit(ui_state_t *state)
{
    if (state->page != PAGE_VALUE && state->page != PAGE_TRANSPORT &&
        state->page != PAGE_ENDPOINT)
        return;
    state->return_page = state->page;
    state->edit_original = current_value(state);
    state->edit_staged = state->edit_original;
    state->page = PAGE_EDIT;
    printf("edit=START port=%s field=%s original=%d\n",
           config_port_name(state->port_index), value_title(state->value_kind),
           state->edit_original);
}

static int handle_key(ui_state_t *state, int key)
{
    if (state->page == PAGE_HOME) {
        state->page = PAGE_ROOT;
        state->selected = 0;
        return 1;
    }
    if (key == KEY0) {
        go_back(state);
    } else if (state->page == PAGE_HELP && (key == KEY1 || key == KEY3)) {
        state->help_page = state->help_page ? 0 : 1;
    } else if (state->page == PAGE_EDIT && (key == KEY1 || key == KEY3)) {
        change_staged(state, key == KEY1 ? -1 : 1);
        printf("edit=STAGED value=%d\n", state->edit_staged);
    } else if (state->page == PAGE_EDIT && key == KEY2) {
        set_current_value(state, state->edit_staged);
        printf("edit=CONFIRMED_IN_MEMORY value=%d hardware_apply=NO\n",
               state->edit_staged);
        state->page = state->return_page;
    } else if (key == KEY1 || key == KEY3) {
        move_selection(state, key == KEY1 ? -1 : 1);
    } else if (key == KEY2) {
        select_item(state);
    } else if (key == KEY4) {
        if (state->page == PAGE_VALUE || state->page == PAGE_TRANSPORT ||
            state->page == PAGE_ENDPOINT)
            start_edit(state);
        else if (state->page == PAGE_ROOT) {
            state->page = PAGE_ABOUT;
            state->selected = 0;
        }
    }
    return 1;
}

static int draw_page(int fd, ui_state_t *state)
{
    ui_screen_t screen;
    render(state, &screen);
    if (lcm_device_draw(fd, &screen) < 0)
        return -1;
    printf("page=%d draw=OK\n", (int)state->page);
    return 0;
}

int main(void)
{
    int lcm_fd;
    int keypad_fd;
    int key;
    int result;
    long ticks_per_second;
    clock_t last_action;
    clock_t last_key = 0;
    ui_state_t state;
    ui_screen_t splash;

    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(&state, 0, sizeof(state));
    configuration_defaults(&state.config);
    state.page = PAGE_HOME;

    printf("product=%s\nversion=%s\ntest=UI configuration prototype\n",
           FOURVRS_PRODUCT_NAME, FOURVRS_VERSION);
    ticks_per_second = sysconf(_SC_CLK_TCK);
    if (ticks_per_second <= 0)
        return 1;
    lcm_fd = lcm_device_open();
    printf("lcm_open=%s autoscroll=OFF\n", lcm_fd >= 0 ? "OK" : "FAIL");
    if (lcm_fd < 0)
        return 2;
    keypad_fd = keypad_device_open();
    printf("keypad_open=%s\n", keypad_fd >= 0 ? "OK" : "FAIL");
    if (keypad_fd < 0) {
        lcm_device_close(lcm_fd);
        return 3;
    }

    ui_screen_clear(&splash);
    ui_screen_set(&splash, 0, "      4VRS");
    ui_screen_set(&splash, 1, " CONFIGURATION");
    ui_screen_set(&splash, 3, "  " FOURVRS_VERSION);
    ui_screen_set(&splash, 5, " Initializing...");
    if (lcm_device_draw(lcm_fd, &splash) < 0)
        return 4;
    sleep(UI_SPLASH_SECONDS);
    if (draw_page(lcm_fd, &state) < 0)
        return 5;
    if (keypad_device_flush(keypad_fd) < 0)
        return 6;

    last_action = now_ticks();
    for (;;) {
        result = keypad_device_get(keypad_fd, &key);
        if (result < 0)
            return 7;
        if (result == 0) {
            if (state.page != PAGE_HOME && state.page != PAGE_EDIT &&
                expired(last_action, UI_NAV_IDLE_SECONDS, ticks_per_second)) {
                state.page = PAGE_HOME;
                state.selected = 0;
                printf("idle=HOME timeout_seconds=%d\n", UI_NAV_IDLE_SECONDS);
                if (draw_page(lcm_fd, &state) < 0)
                    return 8;
            }
            usleep(UI_POLL_USEC);
            continue;
        }
        if (last_key != 0 && now_ticks() - last_key < ticks_per_second / 5)
            continue;
        last_key = now_ticks();
        last_action = last_key;
        printf("raw_key=%d\n", key);
        handle_key(&state, key);
        if (draw_page(lcm_fd, &state) < 0) {
            fprintf(stderr, "error=LCM draw errno=%d\n", errno);
            return 8;
        }
    }
}
