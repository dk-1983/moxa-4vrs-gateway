/* Production specialization; qualified against Python/OpenSSL and upstream vectors.
 * PBKDF2-HMAC-SHA256, one 32-byte block, 16-byte salt, 12..128-byte password.
 * Cache SHA-256 states after the fixed HMAC pads; keep every PBKDF2 iteration.
 * All SHA operations use the pinned Mbed TLS public API. */
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
static int cached_pbkdf2(const unsigned char *password,size_t plen,
                         const unsigned char *salt,unsigned int rounds,
                         unsigned char output[32])
{
    mbedtls_sha256_context inner,outer,work;
    unsigned char key[64]={0},ipad[64],opad[64],u[32],acc[32],first[20];
    unsigned int i,j;int r=-1;
    if(plen<12||plen>128||!rounds)return -1;
    mbedtls_sha256_init(&inner);mbedtls_sha256_init(&outer);mbedtls_sha256_init(&work);
#define KDF_TRY(call) do {if((r=(call))!=0)goto done;}while(0)
    if(plen>64){KDF_TRY(mbedtls_sha256(password,plen,key,0));}
    else memcpy(key,password,plen);
    for(i=0;i<64;i++){ipad[i]=key[i]^0x36;opad[i]=key[i]^0x5c;}
    KDF_TRY(mbedtls_sha256_starts(&inner,0));KDF_TRY(mbedtls_sha256_update(&inner,ipad,64));
    KDF_TRY(mbedtls_sha256_starts(&outer,0));KDF_TRY(mbedtls_sha256_update(&outer,opad,64));
    memcpy(first,salt,16);first[16]=first[17]=first[18]=0;first[19]=1;
    memset(acc,0,sizeof(acc));
    for(i=0;i<rounds;i++){
        mbedtls_sha256_clone(&work,&inner);
        KDF_TRY(mbedtls_sha256_update(&work,i?u:first,i?32:20));
        KDF_TRY(mbedtls_sha256_finish(&work,u));
        mbedtls_sha256_clone(&work,&outer);
        KDF_TRY(mbedtls_sha256_update(&work,u,32));
        KDF_TRY(mbedtls_sha256_finish(&work,u));
        for(j=0;j<32;j++)acc[j]^=u[j];
    }
    memcpy(output,acc,32);r=0;
done:
    mbedtls_sha256_free(&inner);mbedtls_sha256_free(&outer);mbedtls_sha256_free(&work);
    mbedtls_platform_zeroize(key,sizeof(key));mbedtls_platform_zeroize(ipad,sizeof(ipad));
    mbedtls_platform_zeroize(opad,sizeof(opad));mbedtls_platform_zeroize(u,sizeof(u));
    mbedtls_platform_zeroize(acc,sizeof(acc));mbedtls_platform_zeroize(first,sizeof(first));
    if(r)mbedtls_platform_zeroize(output,32);
    return r;
#undef KDF_TRY
}
