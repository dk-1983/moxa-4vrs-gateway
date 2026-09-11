#ifndef FOURVRS_CONFIG_MODEL_H
#define FOURVRS_CONFIG_MODEL_H

#define FOURVRS_PORT_COUNT 8
#define FOURVRS_BAUD_COUNT 20

typedef enum serial_mode {
    SERIAL_MODE_RS232 = 0,
    SERIAL_MODE_RS485_2W = 1,
    SERIAL_MODE_RS422 = 2,
    SERIAL_MODE_RS485_4W = 3,
    SERIAL_MODE_COUNT
} serial_mode_t;

typedef enum parity_mode {
    PARITY_NONE = 0,
    PARITY_EVEN,
    PARITY_ODD,
    PARITY_COUNT
} parity_mode_t;

typedef enum transport_type {
    TRANSPORT_MODBUS_TCP = 0,
    TRANSPORT_MODBUS_UDP,
    TRANSPORT_RTU_UDP,
    TRANSPORT_RAW_TCP,
    TRANSPORT_RAW_UDP,
    TRANSPORT_COUNT
} transport_type_t;

typedef struct port_config {
    serial_mode_t mode;
    int baud_index;
    int data_bits;
    parity_mode_t parity;
    int stop_bits;
    transport_type_t transport;
    int network_port;
    int special_baud_enabled;
    unsigned long special_baud;
} port_config_t;

typedef struct configuration {
    port_config_t port[FOURVRS_PORT_COUNT];
} configuration_t;

void configuration_defaults(configuration_t *config);
const char *config_port_name(int index);
const char *config_device_name(int index);
const char *serial_mode_label(serial_mode_t value);
const char *parity_label(parity_mode_t value);
const char *transport_label(transport_type_t value);
unsigned long baud_value(int index);
int network_port_valid(int value);

#endif
