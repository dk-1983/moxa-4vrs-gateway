#ifndef FOURVRS_RNG_NV_H
#define FOURVRS_RNG_NV_H
#include <stddef.h>
int rng_nv_open(const char *);
int rng_nv_status(void);
int rng_nv_provision(const unsigned char *);
int rng_nv_start(void);
int rng_nv_rotate(void);
int rng_nv_random(unsigned char *,size_t);
int rng_nv_policy(void);
int rng_nv_production(void);
int rng_nv_error(void);
int rng_nv_schema(void);
unsigned long rng_nv_used(void);
int rng_nv_service(const unsigned char *, int);
int rng_nv_finish(void);
int rng_nv_service_finish(void);
unsigned long rng_nv_generation(void);
void rng_nv_close(void);
#endif
