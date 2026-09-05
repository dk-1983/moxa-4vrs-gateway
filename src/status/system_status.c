#define _BSD_SOURCE 1

#include <arpa/inet.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "system_status.h"

static void read_ipv4(int fd, const char *name, char *output, size_t size)
{
    struct ifreq request;
    struct sockaddr_in *address;
    const char *text;

    memset(&request, 0, sizeof(request));
    strncpy(request.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFADDR, &request) < 0) {
        strncpy(output, "NO ADDRESS", size - 1);
        output[size - 1] = '\0';
        return;
    }
    address = (struct sockaddr_in *)&request.ifr_addr;
    text = inet_ntoa(address->sin_addr);
    strncpy(output, text, size - 1);
    output[size - 1] = '\0';
}

void home_status_read(home_status_t *status)
{
    int fd;
    strcpy(status->eth0_ipv4, "NO ADDRESS");
    strcpy(status->eth1_ipv4, "NO ADDRESS");
    status->wall_clock_trusted = 0;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return;
    read_ipv4(fd, "eth0", status->eth0_ipv4, sizeof(status->eth0_ipv4));
    read_ipv4(fd, "eth1", status->eth1_ipv4, sizeof(status->eth1_ipv4));
    close(fd);
}
