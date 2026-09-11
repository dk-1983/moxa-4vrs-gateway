#ifndef FOURVRS_GATEWAY_NETWORK_SETTINGS_H
#define FOURVRS_GATEWAY_NETWORK_SETTINGS_H

#define GATEWAY_LAN_COUNT 2U
#define GATEWAY_IPV4_TEXT_MAX 16U

typedef enum gateway_lan_mode {
    GATEWAY_LAN_STATIC = 0,
    GATEWAY_LAN_DHCP_CLIENT
} gateway_lan_mode_t;

typedef struct gateway_lan_settings {
    gateway_lan_mode_t mode;
    char address[GATEWAY_IPV4_TEXT_MAX];
    char netmask[GATEWAY_IPV4_TEXT_MAX];
    char gateway[GATEWAY_IPV4_TEXT_MAX];
} gateway_lan_settings_t;

typedef struct gateway_network_settings {
    gateway_lan_settings_t lan[GATEWAY_LAN_COUNT];
    /* 0: no default route; 1: LAN1; 2: LAN2. */
    unsigned int default_lan;
    /* 0: manual DNS; 1: explicitly selected DHCP DNS source. */
    unsigned int automatic_dns;
    /* 1/2: source independent of route. 0 preserves PROFILE_1's route choice. */
    unsigned int dns_lan;
    char dns[2][GATEWAY_IPV4_TEXT_MAX];
} gateway_network_settings_t;

typedef enum gateway_network_settings_result {
    GATEWAY_NETWORK_SETTINGS_OK = 0,
    GATEWAY_NETWORK_SETTINGS_INVALID,
    GATEWAY_NETWORK_SETTINGS_ADDRESS,
    GATEWAY_NETWORK_SETTINGS_MASK,
    GATEWAY_NETWORK_SETTINGS_GATEWAY,
    GATEWAY_NETWORK_SETTINGS_OVERLAP,
    GATEWAY_NETWORK_SETTINGS_DNS
} gateway_network_settings_result_t;

/* No installed defaults: import existing network before offering Apply. */
void gateway_network_settings_init(gateway_network_settings_t *settings);
/* Bounded decimal parser, host-order value; rejects signs and leading zeros. */
void gateway_ipv4_format(unsigned long address,char output[16]);
int gateway_ipv4_parse(const char text[GATEWAY_IPV4_TEXT_MAX], unsigned long *value);
gateway_network_settings_result_t gateway_network_settings_validate(
    const gateway_network_settings_t *settings, unsigned int *invalid_lan);

#endif
