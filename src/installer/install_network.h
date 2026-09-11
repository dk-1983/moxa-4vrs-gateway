#ifndef FOURVRS_INSTALL_NETWORK_H
#define FOURVRS_INSTALL_NETWORK_H
#include "installer/install_transaction.h"
#include "network/gateway_network_runtime.h"
/* Work directory is private and empty except for an empty store directory.
 * Production observes the real kernel; fixtures substitute those callbacks.
 * All import/boot writes are restricted to this private work directory. */
int install_network_plan(const char *root,const char *work,
                        const gateway_network_environment_t *,install_plan_t *,const char **stage);
#endif
