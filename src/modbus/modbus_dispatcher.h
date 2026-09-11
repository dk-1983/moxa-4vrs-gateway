#ifndef FOURVRS_MODBUS_DISPATCHER_H
#define FOURVRS_MODBUS_DISPATCHER_H

#include "modbus/modbus_tcp_adapter.h"

#define MODBUS_CLIENT_MAX 8U
#define MODBUS_CLIENT_QUEUE_MAX 2U

typedef struct modbus_client_slot {
    unsigned int id;
    unsigned int epoch;
    int connected;
    modbus_tcp_request_t requests[MODBUS_CLIENT_QUEUE_MAX];
    unsigned int head;
    unsigned int count;
} modbus_client_slot_t;

typedef struct modbus_dispatcher {
    modbus_client_slot_t clients[MODBUS_CLIENT_MAX];
    unsigned int next_client;
    unsigned int queue_full;
    unsigned int disconnected_drops;
    unsigned int submitted;
} modbus_dispatcher_t;

void modbus_dispatcher_init(modbus_dispatcher_t *dispatcher);
int modbus_dispatcher_connect(modbus_dispatcher_t *dispatcher,
                              unsigned int client_id, unsigned int epoch);
void modbus_dispatcher_disconnect(modbus_dispatcher_t *dispatcher,
                                  unsigned int client_id, unsigned int epoch);
int modbus_dispatcher_enqueue(modbus_dispatcher_t *dispatcher,
                              const modbus_tcp_request_t *request);
core_submit_result_t modbus_dispatcher_step(modbus_dispatcher_t *dispatcher,
                                            port_runtime_t *port,
                                            core_tick_t now,
                                            core_tick_t queue_timeout,
                                            core_tick_t response_timeout);
int modbus_dispatcher_client_current(const modbus_dispatcher_t *dispatcher,
                                     unsigned int client_id,
                                     unsigned int epoch);

#endif
