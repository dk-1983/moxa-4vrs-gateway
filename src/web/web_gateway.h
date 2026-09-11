#ifndef FOURVRS_WEB_GATEWAY_H
#define FOURVRS_WEB_GATEWAY_H
#include "web/web_security.h"
#include "web/web_ipc_endpoint.h"
#include "app/gateway_application.h"
typedef struct web_gateway {
 gateway_application_t *app;web_security_t security;
 char directory[256],socket_path[108],binary[256],addresses[2][16];
 int listener,peer,pid,auth_pending,stopping,initialized;
 web_ipc_endpoint_t ipc_endpoint;
 int rng_pid,rng_web,rng_epoch_set,rng_stopping,rng_attaching,rng_pending_fd,rng_fault;uint32_t rng_retry;
 unsigned int protocol_seen,launch_protocol,switch_from,switch_pending,switch_failed;uint32_t switch_at;
 unsigned int failures,https_port,http_port,peer_uid,auto_code_issued;
 uint32_t retry_at,started_at,last_launch_at,peer_at,recovery_at,recovery_rate_at;
 web_frame_t incoming,outgoing;
#ifdef WEB_HOST_TEST
 char test_addresses[2][16]; /* fake interface provider, absent from target */
#endif
} web_gateway_t;
int web_gateway_init(web_gateway_t *,gateway_application_t *,const char *,const char *,unsigned int,unsigned int);
void web_gateway_step(web_gateway_t *);
void web_gateway_close(web_gateway_t *);
int web_gateway_local(void *,unsigned int);
/* Same fail-closed network gate used before starting/rebinding Web. */
unsigned int web_gateway_network_error(const gateway_network_runtime_t *,unsigned int,char [2][16]);
/* Dispatcher is the same owner-side implementation used by live IPC/tests. */
int web_gateway_request(web_gateway_t *,const char *,size_t,char *,size_t,uint32_t);
#endif
