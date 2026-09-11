#ifndef FOURVRS_INSTALL_ORCHESTRATOR_H
#define FOURVRS_INSTALL_ORCHESTRATOR_H
#include "installer/install_package.h"
#include "installer/install_network.h"
#include "installer/install_bootstrap.h"
typedef struct install_platform {
 int (*detect)(void *);
 int (*cf_available)(void *);
 int (*inspect)(void *,unsigned int *running);
 int (*stop)(void *);
 int (*start)(void *,unsigned int old_set);
 int (*verify)(void *,unsigned int old_set);
 int (*entry)(void *,unsigned int application,const char *action,unsigned int fallback);
 int (*capacity)(void *,size_t early_bytes,size_t cf_bytes);
} install_platform_t;
typedef struct install_context {
 const char *root;
 const install_platform_t *platform;
 void *platform_context;
 int (*restore_apache)(unsigned int);
 const gateway_network_environment_t *network;
 char init_directory[64],state_directory[1024],journal_directory[1024];
 char slot[8];
 unsigned int entry_mode, restore_running, transaction_services;
 const char *stage;
 int detail;
 const char *failure_stage;
 int failure_detail;
 /* Public decision diagnostics: no file contents or RNG material. */
 const char *decision, *decision_health, *verify_reason;
 char decision_path[128];
 unsigned int decision_count, decision_mask;
 install_plan_t plan;
 install_transaction_t transaction;
 /* Qualification only: public main never sets this callback. */
 int (*boundary)(void *,const char *,unsigned int);
 void *boundary_context;
} install_context_t;
int install_layout(install_context_t *,unsigned int create);
int install_orchestrate(install_context_t *,const install_package_t *);
int install_recover_entry(install_context_t *,unsigned int application,const char *action);
int install_recover_only(install_context_t *);
int install_allow_path(void *,const char *,unsigned int);
void install_context_release(install_context_t *);
#endif
