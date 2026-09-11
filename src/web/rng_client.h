#ifndef FOURVRS_RNG_CLIENT_H
#define FOURVRS_RNG_CLIENT_H
#include <stddef.h>
#include <stdint.h>
/* One private channel per exec process. Gateway uses cached mode only. */
void rng_client_set(int fd,int synchronous);
void rng_client_close(void);
int rng_client_step(uint32_t now);
int rng_client_ready(void);
int rng_client_random(void *,size_t);
unsigned int rng_client_epoch(void);
int rng_client_attach(int private_web_fd);
#endif
