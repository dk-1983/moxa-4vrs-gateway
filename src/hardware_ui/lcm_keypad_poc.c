#define _BSD_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/times.h>
#include <unistd.h>

#include <moxadevice.h>

#include "version.h"

#define LCM_COLUMNS 16
#define LCM_ROWS 8
#define MENU_ITEMS 4
#define POLL_USEC 20000
#define SPLASH_SECONDS 2
#define IDLE_SECONDS 300
static const char *menu_items[MENU_ITEMS] = {
    "System", "Network", "Ports", "About"
};

static clock_t monotonic_ticks(void)
{
    struct tms ignored;
    return times(&ignored);
}

static int elapsed(clock_t start, long seconds, long ticks_per_second)
{
    return monotonic_ticks() - start >= (clock_t)(seconds * ticks_per_second);
}

static int lcm_goto(int fd, int x, int y)
{
    lcm_xy_t position;
    position.x = x;
    position.y = y;
    return ioctl(fd, IOCTL_LCM_GOTO_XY, &position);
}

static int lcm_line(int fd, int row, const char *text)
{
    char output[LCM_COLUMNS];
    size_t length = strlen(text);

    if (length > LCM_COLUMNS)
        length = LCM_COLUMNS;
    memset(output, ' ', sizeof(output));
    memcpy(output, text, length);

    if (lcm_goto(fd, 0, row) < 0)
        return -1;
    if (write(fd, output, sizeof(output)) != (ssize_t)sizeof(output))
        return -1;
    return 0;
}

static int lcm_screen(int fd, const char *lines[LCM_ROWS])
{
    int row;
    if (ioctl(fd, IOCTL_LCM_CLS, 0) < 0)
        return -1;
    for (row = 0; row < LCM_ROWS; ++row) {
        if (lcm_line(fd, row, lines[row]) < 0)
            return -1;
    }
    return 0;
}

static int show_splash(int fd)
{
    const char *lines[LCM_ROWS] = {
        "      4VRS", " CONFIGURATION", "", "  " FOURVRS_VERSION,
        "", " Initializing...", "", ""
    };
    return lcm_screen(fd, lines);
}

static int show_menu(int fd, int selected)
{
    char rows[MENU_ITEMS][LCM_COLUMNS + 1];
    const char *lines[LCM_ROWS];
    int item;

    lines[0] = FOURVRS_VERSION;
    lines[1] = "";
    for (item = 0; item < MENU_ITEMS; ++item) {
        rows[item][0] = item == selected ? '>' : ' ';
        rows[item][1] = ' ';
        strncpy(rows[item] + 2, menu_items[item], LCM_COLUMNS - 2);
        rows[item][LCM_COLUMNS] = '\0';
        lines[item + 2] = rows[item];
    }
    lines[6] = "";
    lines[7] = "";
    return lcm_screen(fd, lines);
}

static int show_about(int fd)
{
    const char *lines[LCM_ROWS] = {
        "4VRS CONFIG", FOURVRS_VERSION, "UC-7420-LX+", "DEV BUILD",
        "", "", "", ""
    };
    return lcm_screen(fd, lines);
}

static int show_placeholder(int fd, int selected)
{
    const char *lines[LCM_ROWS];

    lines[0] = "4VRS CONFIG";
    lines[1] = menu_items[selected];
    lines[2] = "";
    lines[3] = "POC ONLY";
    lines[4] = "No configuration";
    lines[5] = "";
    lines[6] = "";
    lines[7] = "";
    return lcm_screen(fd, lines);
}

static int get_key(int fd, int *key)
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

static int flush_keys(int fd)
{
    int key;
    int result;
    do {
        result = get_key(fd, &key);
    } while (result > 0);
    return result;
}

int main(void)
{
    int lcm_fd;
    int keypad_fd;
    int key;
    int key_result;
    int selected = 0;
    int page = 0;
    int exit_code = 0;
    long ticks_per_second;
    clock_t started;
    clock_t last_key = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("product=%s\nversion=%s\ntest=LCM keypad POC\n",
           FOURVRS_PRODUCT_NAME, FOURVRS_VERSION);

    ticks_per_second = sysconf(_SC_CLK_TCK);
    if (ticks_per_second <= 0) {
        fprintf(stderr, "error=sysconf CLK_TCK failed\n");
        return 1;
    }

    lcm_fd = open("/dev/lcm", O_RDWR);
    printf("lcm_open=%s\n", lcm_fd >= 0 ? "OK" : "FAIL");
    if (lcm_fd < 0) {
        fprintf(stderr, "lcm_errno=%d\n", errno);
        return 2;
    }

    keypad_fd = open("/dev/keypad", O_RDWR);
    printf("keypad_open=%s\n", keypad_fd >= 0 ? "OK" : "FAIL");
    if (keypad_fd < 0) {
        fprintf(stderr, "keypad_errno=%d\n", errno);
        close(lcm_fd);
        return 3;
    }

    if (ioctl(lcm_fd, IOCTL_LCM_BACK_LIGHT_ON, 0) < 0)
        printf("lcm_backlight=UNAVAILABLE errno=%d\n", errno);
    else
        printf("lcm_backlight=OK\n");

    if (ioctl(lcm_fd, IOCTL_LCM_AUTO_SCROLL_OFF, 0) < 0) {
        fprintf(stderr, "error=autoscroll off failed errno=%d\n", errno);
        exit_code = 4;
        goto done;
    }
    printf("lcm_autoscroll=OFF\n");

    if (show_splash(lcm_fd) < 0) {
        fprintf(stderr, "error=splash write failed errno=%d\n", errno);
        exit_code = 4;
        goto done;
    }
    printf("splash_write=OK\n");
    started = monotonic_ticks();
    while (!elapsed(started, SPLASH_SECONDS, ticks_per_second))
        usleep(POLL_USEC);

    if (show_menu(lcm_fd, selected) < 0) {
        fprintf(stderr, "error=menu write failed errno=%d\n", errno);
        exit_code = 5;
        goto done;
    }
    printf("menu_write=OK selection=%s\n", menu_items[selected]);

    if (flush_keys(keypad_fd) < 0) {
        fprintf(stderr, "error=keypad flush failed errno=%d\n", errno);
        exit_code = 6;
        goto done;
    }

    started = monotonic_ticks();
    for (;;) {
        if (elapsed(started, IDLE_SECONDS, ticks_per_second)) {
            printf("result=TIMEOUT\n");
            break;
        }

        key_result = get_key(keypad_fd, &key);
        if (key_result < 0) {
            fprintf(stderr, "error=keypad read failed errno=%d\n", errno);
            exit_code = 7;
            break;
        }
        if (key_result == 0) {
            usleep(POLL_USEC);
            continue;
        }

        if (last_key != 0 &&
            monotonic_ticks() - last_key < ticks_per_second / 5)
            continue;
        last_key = monotonic_ticks();
        started = last_key;

        printf("raw_key=%d\n", key);

        if (key == KEY0) {
            printf("key=KEY0 physical=F1 action=BACK\n");
            if (page != 0) {
                page = 0;
                if (show_menu(lcm_fd, selected) < 0)
                    exit_code = 8;
            }
        } else if (page == 0 && key == KEY1) {
            if (selected > 0)
                --selected;
            printf("key=KEY1 physical=F2 action=UP selection=%s\n",
                   menu_items[selected]);
            if (show_menu(lcm_fd, selected) < 0)
                exit_code = 8;
        } else if (page == 0 && key == KEY3) {
            if (selected < MENU_ITEMS - 1)
                ++selected;
            printf("key=KEY3 physical=F4 action=DOWN selection=%s\n",
                   menu_items[selected]);
            if (show_menu(lcm_fd, selected) < 0)
                exit_code = 8;
        } else if (page == 0 && key == KEY2) {
            printf("key=KEY2 physical=F3 action=SELECT selection=%s\n",
                   menu_items[selected]);
            if (selected == MENU_ITEMS - 1) {
                page = 1;
                if (show_about(lcm_fd) < 0)
                    exit_code = 8;
            } else {
                page = 2;
                if (show_placeholder(lcm_fd, selected) < 0)
                    exit_code = 8;
            }
        } else if (key == KEY4) {
            printf("key=KEY4 physical=F5 action=CONTEXT\n");
            if (page == 0) {
                page = 1;
                if (show_about(lcm_fd) < 0)
                    exit_code = 8;
            } else if (page == 1) {
                printf("context=ABOUT_REFRESH\n");
                if (show_about(lcm_fd) < 0)
                    exit_code = 8;
            } else {
                printf("context=DETAILS_REFRESH\n");
                if (show_placeholder(lcm_fd, selected) < 0)
                    exit_code = 8;
            }
        } else {
            printf("key=%d action=NOOP_ON_PAGE page=%d\n", key, page);
        }

        if (exit_code != 0) {
            fprintf(stderr, "error=LCM update failed errno=%d\n", errno);
            break;
        }
    }

done:
    close(keypad_fd);
    close(lcm_fd);
    printf("status=%s\n", exit_code == 0 ? "OK" : "FAIL");
    return exit_code;
}
