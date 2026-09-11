#ifndef FOURVRS_RNG_WIRE_H
#define FOURVRS_RNG_WIRE_H
#define RNG_MAX 1024U
#define RNG_HEADER 16U
/* Waiting for a durable NV advance is distinct from partial-frame transport. */
#define RNG_COMMIT_WAIT_MS 30000U
#define RNG_FRAME_WAIT_MS 3000U
static unsigned int rng_get(const unsigned char *p){return ((unsigned int)p[0]<<24)|((unsigned int)p[1]<<16)|((unsigned int)p[2]<<8)|p[3];}
static void rng_put(unsigned char *p,unsigned int n){p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;}
#endif
