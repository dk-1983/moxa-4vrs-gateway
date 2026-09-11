#ifndef FOURVRS_GATEWAY_PANEL_MOXA_H
#define FOURVRS_GATEWAY_PANEL_MOXA_H

#include "panel/gateway_panel.h"

typedef struct gateway_panel_moxa {
    int display_fd;
    int keypad_fd;
} gateway_panel_moxa_t;

void gateway_panel_moxa_context_init(gateway_panel_moxa_t *context);
const gateway_panel_ops_t *gateway_panel_moxa_ops(void);

#endif
