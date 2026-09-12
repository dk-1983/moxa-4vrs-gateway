#ifndef FOURVRS_NETWORK_BOOT_H
#define FOURVRS_NETWORK_BOOT_H
#include <stddef.h>
#include "network/gateway_network_service.h"
typedef int (*gateway_network_unowned_action_fn)(void *,const char *,unsigned int up);
typedef struct gateway_network_boot_ops {
    int (*present)(void *,const char *); /* 1 present, 0 absent, <0 inventory error */
    gateway_network_unowned_action_fn action;
    void (*diagnostic)(void *,const char *,const char *,int);
    int (*loopback)(void *,const char *,size_t,unsigned int);
} gateway_network_boot_ops_t;
typedef struct gateway_network_loopback_ops {
    int (*observe)(void *); /* 0 no address/down, 1 exact ready, 2 other,
                            * 3 no address/UP, 4 exact address/down,
                            * -1 observation error */
    int (*receipt)(void *,unsigned int,const char *,size_t); /* 0 match, 1 clear, 2 save; match returns 0/1 */
    int (*action)(void *,unsigned int); /* vendor up, or forced vendor down */
    void (*diagnostic)(void *,const char *,const char *,int);
} gateway_network_loopback_ops_t;
int gateway_network_loopback(const char *,size_t,unsigned int,const gateway_network_loopback_ops_t *,void *);
int gateway_network_loopback_observe(void *);
int gateway_network_loopback_receipt(void *,unsigned int,const char *,size_t);
int gateway_network_unowned_checked(const char *,size_t,unsigned int,const gateway_network_boot_ops_t *,void *);
int gateway_network_vendor_boot_with_ops(const char *,unsigned int,const gateway_network_boot_ops_t *,void *);
int gateway_network_vendor_present(void *,const char *);
/* Isolated vendor process; waits are bounded 5ms polls, maximum 2000.
 * Return exit code, 128+signal, -1 setup error, -2 reaped timeout,
 * -3 timeout with unresolved reap. */
int gateway_network_vendor_execute(const char *,const char *,unsigned int waits);
int gateway_network_vendor_execute_forced(const char *,const char *,unsigned int waits);
int gateway_network_boot_service_with_environment(const gateway_network_service_environment_t *);
/* Enumerate data, never evaluate hooks here. The vendor ifup/ifdown applet
 * remains responsible for each explicitly named unowned interface. */
int gateway_network_unowned(const char *,size_t,unsigned int,gateway_network_unowned_action_fn,void *);
int gateway_network_vendor_boot(const char *store,unsigned int up);
int gateway_network_boot_service(void);
#endif
