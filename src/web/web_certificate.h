#ifndef FOURVRS_WEB_CERTIFICATE_H
#define FOURVRS_WEB_CERTIFICATE_H
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
typedef struct web_certificate {mbedtls_entropy_context entropy;mbedtls_ctr_drbg_context random;mbedtls_pk_context key;mbedtls_x509_crt cert;mbedtls_ssl_config config;} web_certificate_t;
int web_certificate_open(web_certificate_t *,const char *,const char *,const char *);
void web_certificate_close(web_certificate_t *);
int web_certificate_due(const web_certificate_t *);
int web_certificate_fingerprint(const web_certificate_t *,char out[65]);
#endif
