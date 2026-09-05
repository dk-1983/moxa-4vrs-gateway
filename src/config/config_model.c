#include "config_model.h"

static const char *port_names[FOURVRS_PORT_COUNT] = {
    "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"
};

static const char *device_names[FOURVRS_PORT_COUNT] = {
    "ttyM0", "ttyM1", "ttyM2", "ttyM3",
    "ttyM4", "ttyM5", "ttyM6", "ttyM7"
};

static const char *mode_labels[SERIAL_MODE_COUNT] = {
    "RS232", "RS485-2W", "RS422", "RS485-4W"
};

static const char *parity_labels[PARITY_COUNT] = {
    "None", "Even", "Odd"
};

static const char *transport_labels[TRANSPORT_COUNT] = {
    "MB TCP", "MB UDP", "RTU/UDP", "RAW TCP", "RAW UDP"
};

static const unsigned long baud_values[FOURVRS_BAUD_COUNT] = {
    50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800,
    2400, 4800, 9600, 19200, 38400, 57600, 115200,
    230400, 460800, 921600
};

void configuration_defaults(configuration_t *config)
{
    int index;
    for (index = 0; index < FOURVRS_PORT_COUNT; ++index) {
        config->port[index].mode = SERIAL_MODE_RS232;
        config->port[index].baud_index = 12;
        config->port[index].data_bits = 8;
        config->port[index].parity = PARITY_NONE;
        config->port[index].stop_bits = 1;
        config->port[index].transport = TRANSPORT_MODBUS_TCP;
        config->port[index].network_port = 502 + index;
        config->port[index].special_baud_enabled = 0;
        config->port[index].special_baud = 0;
    }
}

const char *config_port_name(int index)
{
    return index >= 0 && index < FOURVRS_PORT_COUNT ? port_names[index] : "P?";
}

const char *config_device_name(int index)
{
    return index >= 0 && index < FOURVRS_PORT_COUNT ? device_names[index] : "ttyM?";
}

const char *serial_mode_label(serial_mode_t value)
{
    int index = (int)value;
    return index >= 0 && index < SERIAL_MODE_COUNT ? mode_labels[index] : "Invalid";
}

const char *parity_label(parity_mode_t value)
{
    int index = (int)value;
    return index >= 0 && index < PARITY_COUNT ? parity_labels[index] : "Invalid";
}

const char *transport_label(transport_type_t value)
{
    int index = (int)value;
    return index >= 0 && index < TRANSPORT_COUNT ? transport_labels[index] : "Invalid";
}

unsigned long baud_value(int index)
{
    return index >= 0 && index < FOURVRS_BAUD_COUNT ? baud_values[index] : 0;
}

int network_port_valid(int value)
{
    return value >= 1 && value <= 65535;
}
