#ifndef FOURVRS_GATEWAY_PANEL_MOXA_H
#define FOURVRS_GATEWAY_PANEL_MOXA_H

#include "panel/gateway_panel.h"

typedef struct gateway_panel_moxa {
    int display_fd;
    int keypad_fd;
    volatile const unsigned char *key_register;
} gateway_panel_moxa_t;

int gateway_panel_moxa_key_state(void *, unsigned int *);
void gateway_panel_moxa_context_init(gateway_panel_moxa_t *context);
const gateway_panel_ops_t *gateway_panel_moxa_ops(void);

#endif
