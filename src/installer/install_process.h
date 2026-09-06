#ifndef FOURVRS_INSTALL_PROCESS_H
#define FOURVRS_INSTALL_PROCESS_H
#include <sys/types.h>
typedef struct install_process {pid_t pid;dev_t device;ino_t inode;unsigned long long starttime;} install_process_t;
/* No cached PID is signaled without matching executable inode AND starttime.
 * A zombie counts as stopped; the direct-child runner still performs waitpid. */
int install_process_capture(pid_t,const char *,install_process_t *);
int install_process_matches(const install_process_t *);
int install_process_stop(const install_process_t *,unsigned int timeout_ms);
/* Runs a synchronous, non-daemonizing command in a private session. Timeout
 * kills only that created process group and reaps the direct child. Commands
 * that deliberately create other sessions require a separate ownership
 * provider; this function is not their lifecycle manager. */
int install_process_run(const char *,char *const [],unsigned int timeout_ms);
/* Parent receives the created PID, never a completion claim. Only the
 * exclusive lock fd survives detachment; completion is persisted by work. */
int install_process_detach(int lock_fd,int (*work)(void *),void *,pid_t *);
#endif
