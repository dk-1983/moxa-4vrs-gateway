#ifndef FOURVRS_INSTALL_BOOTSTRAP_H
#define FOURVRS_INSTALL_BOOTSTRAP_H
#include "installer/install_transaction.h"
#define INSTALL_RECOVERY_DIRECTORY "etc/4vrs-installer"
#define INSTALL_RECOVERY_EXECUTABLE "/etc/4vrs-installer/recovery"
/* Called only after package and current executable validation. The private
 * directory is pre-created by the layout provider. No active entry is changed
 * here: a failed bootstrap publication leaves the old boot chain intact. */
int install_bootstrap_prepare(const char *,const install_file_t *);
int install_bootstrap_add_gate(install_plan_t *,const char *,const char *,unsigned int application);
#endif
