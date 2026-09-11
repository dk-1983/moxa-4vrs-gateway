#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <moxadevice.h>

#include "keypad_device.h"

int keypad_device_open(void)
{
    return open("/dev/keypad", O_RDWR);
}

void keypad_device_close(int fd)
{
    close(fd);
}

int keypad_device_get(int fd, int *key)
{
    int count = 0;
    if (ioctl(fd, IOCTL_KEYPAD_HAS_PRESS, &count) < 0)
        return -1;
    if (count <= 0)
        return 0;
    if (ioctl(fd, IOCTL_KEYPAD_GET_KEY, key) < 0)
        return -1;
    return 1;
}

int keypad_device_flush(int fd)
{
    int key;
    int result;
    do {
        result = keypad_device_get(fd, &key);
    } while (result > 0);
    return result;
}
