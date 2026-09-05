#include <string.h>

#include "modbus/modbus_dispatcher.h"

static modbus_client_slot_t *find_slot(modbus_dispatcher_t *dispatcher,
                                       unsigned int id)
{
    unsigned int i;
    for (i = 0; i < MODBUS_CLIENT_MAX; ++i)
        if (dispatcher->clients[i].connected && dispatcher->clients[i].id == id)
            return &dispatcher->clients[i];
    return 0;
}

void modbus_dispatcher_init(modbus_dispatcher_t *dispatcher)
{
    memset(dispatcher, 0, sizeof(*dispatcher));
}

int modbus_dispatcher_connect(modbus_dispatcher_t *dispatcher,
                              unsigned int client_id, unsigned int epoch)
{
    unsigned int i;
    modbus_client_slot_t *slot;
    if (dispatcher == 0 || epoch == 0 || find_slot(dispatcher, client_id) != 0)
        return -1;
    for (i = 0; i < MODBUS_CLIENT_MAX; ++i) {
        slot = &dispatcher->clients[i];
        if (!slot->connected) {
            memset(slot, 0, sizeof(*slot));
            slot->connected = 1;
            slot->id = client_id;
            slot->epoch = epoch;
            return 0;
        }
    }
    return -1;
}

void modbus_dispatcher_disconnect(modbus_dispatcher_t *dispatcher,
                                  unsigned int client_id, unsigned int epoch)
{
    modbus_client_slot_t *slot;
    if (dispatcher == 0)
        return;
    slot = find_slot(dispatcher, client_id);
    if (slot != 0 && slot->epoch == epoch) {
        dispatcher->disconnected_drops += slot->count;
        memset(slot, 0, sizeof(*slot));
    }
}

int modbus_dispatcher_enqueue(modbus_dispatcher_t *dispatcher,
                              const modbus_tcp_request_t *request)
{
    modbus_client_slot_t *slot;
    unsigned int tail;
    if (dispatcher == 0 || request == 0)
        return -1;
    slot = find_slot(dispatcher, request->client_id);
    if (slot == 0 || slot->epoch != request->client_epoch)
        return -1;
    if (slot->count == MODBUS_CLIENT_QUEUE_MAX) {
        ++dispatcher->queue_full;
        return -2;
    }
    tail = (slot->head + slot->count) % MODBUS_CLIENT_QUEUE_MAX;
    slot->requests[tail] = *request;
    ++slot->count;
    return 0;
}

core_submit_result_t modbus_dispatcher_step(modbus_dispatcher_t *dispatcher,
                                            port_runtime_t *port,
                                            core_tick_t now,
                                            core_tick_t queue_timeout,
                                            core_tick_t response_timeout)
{
    unsigned int n;
    unsigned int index;
    modbus_client_slot_t *slot;
    core_submit_result_t result;
    if (dispatcher == 0 || port == 0)
        return CORE_INVALID_REQUEST;
    for (n = 0; n < MODBUS_CLIENT_MAX; ++n) {
        index = (dispatcher->next_client + n) % MODBUS_CLIENT_MAX;
        slot = &dispatcher->clients[index];
        if (!slot->connected || slot->count == 0)
            continue;
        result = modbus_tcp_submit(port, &slot->requests[slot->head], now,
                                   queue_timeout, response_timeout, 0);
        dispatcher->next_client = (index + 1U) % MODBUS_CLIENT_MAX;
        if (result == CORE_ACCEPTED) {
            slot->head = (slot->head + 1U) % MODBUS_CLIENT_QUEUE_MAX;
            --slot->count;
            ++dispatcher->submitted;
        }
        return result;
    }
    return CORE_INVALID_REQUEST;
}

int modbus_dispatcher_client_current(const modbus_dispatcher_t *dispatcher,
                                     unsigned int client_id,
                                     unsigned int epoch)
{
    unsigned int i;
    if (dispatcher == 0)
        return 0;
    for (i = 0; i < MODBUS_CLIENT_MAX; ++i)
        if (dispatcher->clients[i].connected &&
            dispatcher->clients[i].id == client_id &&
            dispatcher->clients[i].epoch == epoch)
            return 1;
    return 0;
}
