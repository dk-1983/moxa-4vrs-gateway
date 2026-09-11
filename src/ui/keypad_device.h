#ifndef FOURVRS_KEYPAD_DEVICE_H
#define FOURVRS_KEYPAD_DEVICE_H

int keypad_device_open(void);
void keypad_device_close(int fd);
int keypad_device_get(int fd, int *key);
int keypad_device_flush(int fd);

#endif
