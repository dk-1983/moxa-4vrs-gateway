#ifndef FOURVRS_LCM_DEVICE_H
#define FOURVRS_LCM_DEVICE_H

#include "ui_defaults.h"

typedef struct ui_screen {
    char row[UI_ROWS][UI_COLUMNS + 1];
} ui_screen_t;

int lcm_device_open(void);
void lcm_device_close(int fd);
void ui_screen_clear(ui_screen_t *screen);
void ui_screen_set(ui_screen_t *screen, int row, const char *text);
int lcm_device_draw(int fd, const ui_screen_t *screen);

#endif
