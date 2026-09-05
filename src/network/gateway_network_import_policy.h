#ifndef FOURVRS_NETWORK_IMPORT_POLICY_H
#define FOURVRS_NETWORK_IMPORT_POLICY_H
#include "network/gateway_network_observation.h"
/* Read-only import. Proc argv and vendor lease records are data, never shell. */
int gateway_network_vendor_arguments(const char *,size_t,unsigned int *,unsigned int *,unsigned int *);
int gateway_network_import_policy(const char *proc,const char *leases,gateway_network_settings_t *,const gateway_network_observation_t *);
#endif
