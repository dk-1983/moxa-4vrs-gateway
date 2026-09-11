#ifndef FOURVRS_INSTALL_TRANSACTION_H
#define FOURVRS_INSTALL_TRANSACTION_H
#include "installer/install_files.h"
#define INSTALL_MEMBERS 32U
#define INSTALL_ON_CF 1U
#define INSTALL_BOOT_GATE 2U
typedef struct install_member {
    char path[INSTALL_PATH_LIMIT+1];
    unsigned int compact_flash; /* INSTALL_ON_CF | INSTALL_BOOT_GATE flags */
    install_file_t before,after;
} install_member_t;
typedef struct install_plan {
    unsigned int count,was_running;
    install_member_t member[INSTALL_MEMBERS];
} install_plan_t;
typedef struct install_transaction_ops {
    /* All callbacks are synchronous, bounded and return after all mutating
     * children have exited. They must not retain asynchronous file writers. */
    int (*gate)(void *); /* durable independent recovery entry, before cutover */
    int (*stop)(void *);
    int (*start)(void *,unsigned int old_set);
    int (*verify)(void *,unsigned int old_set);
    int (*cf_available)(void *); /* 1 available, 0 late/absent, -1 error */
    int (*allow_path)(void *,const char *,unsigned int cf);
    int (*boundary)(void *,const char *,unsigned int); /* NULL in production */
} install_transaction_ops_t;
typedef struct install_transaction {
    const char *root,*journal;
    const install_transaction_ops_t *ops;
    void *context;
} install_transaction_t;
enum install_result { INSTALL_COMPLETED=0,INSTALL_ROLLED_BACK=1,
 INSTALL_WAIT_CF=2,INSTALL_UNCHANGED=3,INSTALL_REFUSED=-1,
 INSTALL_RECOVERY_REQUIRED=-2 };
void install_plan_free(install_plan_t *);
int install_plan_add(install_plan_t *,const char *,const char *,unsigned int,const install_file_t *);
int install_transaction_run(install_transaction_t *,install_plan_t *);
/* Always call under the exclusive installer lock, including early boot.
 * Corrupt/missing journal must never be interpreted as completed. */
int install_transaction_recover(install_transaction_t *);
/* Decodes only, with digest and allowlist validation; no recovery side effects. */
int install_transaction_load(install_transaction_t *,install_plan_t *);
int install_transaction_status(install_transaction_t *);
#endif
