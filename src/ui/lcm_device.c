#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <moxadevice.h>

#include "lcm_device.h"

int lcm_device_open(void)
{
    int fd = open("/dev/lcm", O_RDWR);
    if (fd < 0)
        return -1;
    if (ioctl(fd, IOCTL_LCM_AUTO_SCROLL_OFF, 0) < 0) {
        close(fd);
        return -1;
    }
    if (ioctl(fd, IOCTL_LCM_BACK_LIGHT_ON, 0) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void lcm_device_close(int fd)
{
    close(fd);
}

void ui_screen_clear(ui_screen_t *screen)
{
    int row;
    for (row = 0; row < UI_ROWS; ++row) {
        memset(screen->row[row], ' ', UI_COLUMNS);
        screen->row[row][UI_COLUMNS] = '\0';
    }
}

void ui_screen_set(ui_screen_t *screen, int row, const char *text)
{
    size_t length;
    if (row < 0 || row >= UI_ROWS)
        return;
    memset(screen->row[row], ' ', UI_COLUMNS);
    length = strlen(text);
    if (length > UI_COLUMNS)
        length = UI_COLUMNS;
    memcpy(screen->row[row], text, length);
    screen->row[row][UI_COLUMNS] = '\0';
}

int lcm_device_draw(int fd, const ui_screen_t *screen)
{
    int row;
    lcm_xy_t position;

    if (ioctl(fd, IOCTL_LCM_CLS, 0) < 0)
        return -1;
    for (row = 0; row < UI_ROWS; ++row) {
        position.x = 0;
        position.y = row;
        if (ioctl(fd, IOCTL_LCM_GOTO_XY, &position) < 0)
            return -1;
        if (write(fd, screen->row[row], UI_COLUMNS) != UI_COLUMNS)
            return -1;
    }
    return 0;
}
