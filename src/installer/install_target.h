#ifndef FOURVRS_INSTALL_TARGET_H
#define FOURVRS_INSTALL_TARGET_H
#include "installer/install_orchestrator.h"
#include "installer/install_process.h"
typedef struct install_target {
 install_context_t *installer;
 gateway_network_observation_t baseline;
 unsigned int baseline_valid;
 install_process_t application,guardian,owner;
 unsigned int have_application,have_guardian;
} install_target_t;
void install_target_init(install_context_t *,install_target_t *);
int install_target_lock(void);
#endif
