#ifndef FOURVRS_WEB_IPC_ENDPOINT_H
#define FOURVRS_WEB_IPC_ENDPOINT_H
#include <sys/types.h>
typedef struct web_ipc_endpoint {
 int lock_fd,bound;
 dev_t device;
 ino_t inode;
 char path[108],directory[108];
} web_ipc_endpoint_t;
void web_ipc_endpoint_init(web_ipc_endpoint_t *);
int web_ipc_endpoint_open(web_ipc_endpoint_t *,const char *,unsigned long);
void web_ipc_endpoint_close(web_ipc_endpoint_t *,int);
#endif
