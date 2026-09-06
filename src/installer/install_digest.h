#ifndef FOURVRS_INSTALL_DIGEST_H
#define FOURVRS_INSTALL_DIGEST_H
#include <stddef.h>
#include <stdint.h>
typedef struct install_digest {
    uint32_t h[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} install_digest_t;
void install_digest_init(install_digest_t *);
void install_digest_update(install_digest_t *,const void *,size_t);
void install_digest_final(install_digest_t *,unsigned char out[32]);
void install_digest_hex(const void *,size_t,char out[65]);
#endif
