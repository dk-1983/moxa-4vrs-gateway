#include <stdio.h>
#include <sys/utsname.h>

#include "version.h"

int main(void)
{
    const unsigned long marker = 0x01020304UL;
    const unsigned char *bytes = (const unsigned char *)&marker;
    struct utsname system_info;
    int valid = 1;

    printf("product=%s\n", FOURVRS_PRODUCT_NAME);
    printf("version=%s\n", FOURVRS_VERSION);
    printf("test=Target acceptance test\n");
    printf("pointer_bits=%lu\n", (unsigned long)(sizeof(void *) * 8U));

    if (bytes[0] == 0x01U) {
        printf("byte_order=big\n");
    } else if (bytes[0] == 0x04U) {
        printf("byte_order=little\n");
        fprintf(stderr, "failure=expected big-endian byte order\n");
        valid = 0;
    } else {
        printf("byte_order=unknown\n");
        fprintf(stderr, "failure=unable to determine byte order\n");
        valid = 0;
    }

    if (sizeof(void *) != 4U) {
        fprintf(stderr, "failure=expected 32-bit pointers\n");
        valid = 0;
    }

    if (uname(&system_info) == 0) {
        printf("uname_sysname=%s\n", system_info.sysname);
        printf("uname_machine=%s\n", system_info.machine);
    } else {
        fprintf(stderr, "failure=uname failed\n");
        valid = 0;
    }

    printf("status=%s\n", valid ? "OK" : "FAIL");
    return valid ? 0 : 1;
}
