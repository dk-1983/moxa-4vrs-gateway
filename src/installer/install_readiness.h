#ifndef FOURVRS_INSTALL_READINESS_H
#define FOURVRS_INSTALL_READINESS_H
#include <sys/types.h>
/* Read-only structural acceptance of an existing (pre-notification) Gateway:
 * every enabled UART and exact listener must belong to the checked process.
 * This is not instrument communication or a claim about its internal enum. */
int install_readiness(pid_t);
/* Read-only resource observation used by qualification fixtures as well.
 * This never redirects an installation or invokes a network command. */
int install_readiness_at(const char *,pid_t);
#endif
