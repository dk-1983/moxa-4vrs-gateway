#ifndef FOURVRS_INSTALL_PACKAGE_H
#define FOURVRS_INSTALL_PACKAGE_H
#include "installer/install_files.h"
#define INSTALL_RELEASE "v2026.02.03"
#define INSTALL_PAYLOAD_COUNT 7
typedef struct install_package {install_file_t payload[INSTALL_PAYLOAD_COUNT];char digest[INSTALL_PAYLOAD_COUNT][65];} install_package_t;
extern const char *const install_payload_names[INSTALL_PAYLOAD_COUNT];
int install_package_read(const char *,install_package_t *,const char **stage);
int install_package_self(const install_package_t *);
void install_package_free(install_package_t *);
#endif
