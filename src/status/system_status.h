#ifndef FOURVRS_SYSTEM_STATUS_H
#define FOURVRS_SYSTEM_STATUS_H

#define STATUS_IPV4_LENGTH 16

typedef struct home_status {
    char eth0_ipv4[STATUS_IPV4_LENGTH];
    char eth1_ipv4[STATUS_IPV4_LENGTH];
    int wall_clock_trusted;
} home_status_t;

void home_status_read(home_status_t *status);

#endif
