#define _GNU_SOURCE
#include "web/web_certificate.h"
#include "web/web_protocol.h"
#include "mbedtls/ecp.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/sha256.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static union {unsigned char bytes[768U*1024U];uint64_t align;} arena;
int web_certificate_fingerprint(const web_certificate_t *c,char out[65]){unsigned char hash[32];if(mbedtls_sha256(c->cert.raw.p,c->cert.raw.len,hash,0))return -1;web_hex(hash,32,out);return 0;}
int web_certificate_due(const web_certificate_t *c){
 time_t now=time(NULL),horizon;struct tm tm;char current[16],from[16],until[16];
 if(now<1767225600||now>2114380800)return 1;
 if(!gmtime_r(&now,&tm)||!strftime(current,sizeof(current),"%Y%m%d%H%M%S",&tm))return 1;
 snprintf(from,sizeof(from),"%04d%02d%02d%02d%02d%02d",c->cert.valid_from.year,c->cert.valid_from.mon,c->cert.valid_from.day,c->cert.valid_from.hour,c->cert.valid_from.min,c->cert.valid_from.sec);
 if(strcmp(current,from)<0)return 1;
 horizon=now+7*86400;if(!gmtime_r(&horizon,&tm)||!strftime(current,sizeof(current),"%Y%m%d%H%M%S",&tm))return 1;
 snprintf(until,sizeof(until),"%04d%02d%02d%02d%02d%02d",c->cert.valid_to.year,c->cert.valid_to.mon,c->cert.valid_to.day,c->cert.valid_to.hour,c->cert.valid_to.min,c->cert.valid_to.sec);
 return strcmp(current,until)>=0;
}
static int tls_random(void *ctx,unsigned char *out,size_t n){int r=mbedtls_ctr_drbg_random(ctx,out,n);
#ifdef WEB_HOST_TEST
 if(!r)fprintf(stderr,"rng-tls bytes=%lu\n",(unsigned long)n);
#endif
 return r;
}
static int entropy(void *ctx,unsigned char *out,size_t n,size_t *used){(void)ctx;*used=0;if(n>32)n=32;if(web_random(out,n))return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;*used=n;return 0;}
void web_certificate_close(web_certificate_t *c){mbedtls_ssl_config_free(&c->config);mbedtls_x509_crt_free(&c->cert);mbedtls_pk_free(&c->key);mbedtls_ctr_drbg_free(&c->random);mbedtls_entropy_free(&c->entropy);mbedtls_memory_buffer_alloc_free();}
#define TRY(x) do{if((r=(x)))goto done;}while(0)
int web_certificate_open(web_certificate_t *c,const char *dir,const char *ip0,const char *ip1){char path[512],stored[8192],record[8192],names[64],before[16],after[16],*key,*cert,*line;unsigned char pem[4096],keypem[2048],serial_bytes[16],ips[2][4];size_t n;time_t t=time(NULL);struct tm tm;mbedtls_x509write_cert writer;mbedtls_mpi serial;mbedtls_x509_san_list san[2];int r=-1,existing=0,count=0,i,category=20;

#ifdef WEB_HOST_TEST
 fprintf(stderr,"rng-certificate-begin\n");
#endif
 if(t<1767225600||t>2114380800)return 26;mbedtls_memory_buffer_alloc_init(arena.bytes,sizeof(arena.bytes));memset(c,0,sizeof(*c));mbedtls_entropy_init(&c->entropy);mbedtls_ctr_drbg_init(&c->random);mbedtls_pk_init(&c->key);mbedtls_x509_crt_init(&c->cert);mbedtls_ssl_config_init(&c->config);mbedtls_x509write_crt_init(&writer);mbedtls_mpi_init(&serial);
 TRY(mbedtls_entropy_add_source(&c->entropy,entropy,NULL,32,MBEDTLS_ENTROPY_SOURCE_STRONG));TRY(mbedtls_ctr_drbg_seed(&c->random,mbedtls_entropy_func,&c->entropy,(const unsigned char*)"4vrs-web",8));
 category=24;snprintf(path,sizeof(path),"%s/tls.current",dir);snprintf(names,sizeof(names),"%s,%s",ip0,ip1);r=web_read_file(path,stored,sizeof(stored),&n);if(r<0)goto done;if(!r){category=25;if(strncmp(stored,"4VRS_TLS_1\n",11))goto fail;line=strchr(stored+11,'\n');if(!line)goto fail;*line=0;key=line+1;cert=strstr(key,"-----BEGIN CERTIFICATE-----");if(!cert)goto fail;strncpy((char*)pem,cert,sizeof(pem)-1);pem[sizeof(pem)-1]=0;*cert=0;TRY(mbedtls_pk_parse_key(&c->key,(unsigned char*)key,strlen(key)+1,NULL,0,tls_random,&c->random));TRY(mbedtls_x509_crt_parse(&c->cert,pem,strlen((char*)pem)+1));TRY(mbedtls_pk_check_pair(&c->cert.pk,&c->key,tls_random,&c->random));existing=1;
  snprintf(after,sizeof(after),"%04d%02d%02d%02d%02d%02d",c->cert.valid_to.year,c->cert.valid_to.mon,c->cert.valid_to.day,c->cert.valid_to.hour,c->cert.valid_to.min,c->cert.valid_to.sec);if(!gmtime_r(&t,&tm)||!strftime(before,sizeof(before),"%Y%m%d%H%M%S",&tm))goto fail;
  if(!strcmp(names,stored+11)&&!web_certificate_due(c))goto ready;mbedtls_x509_crt_free(&c->cert);mbedtls_x509_crt_init(&c->cert);
 }
 category=20;if(!existing){TRY(mbedtls_pk_setup(&c->key,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)));TRY(mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(c->key),tls_random,&c->random));}
 TRY(tls_random(&c->random,serial_bytes,sizeof(serial_bytes)));serial_bytes[0]=(serial_bytes[0]&0x7f)|1;TRY(mbedtls_mpi_read_binary(&serial,serial_bytes,sizeof(serial_bytes)));mbedtls_x509write_crt_set_version(&writer,MBEDTLS_X509_CRT_VERSION_3);mbedtls_x509write_crt_set_md_alg(&writer,MBEDTLS_MD_SHA256);mbedtls_x509write_crt_set_subject_key(&writer,&c->key);mbedtls_x509write_crt_set_issuer_key(&writer,&c->key);TRY(mbedtls_x509write_crt_set_serial(&writer,&serial));TRY(mbedtls_x509write_crt_set_subject_name(&writer,"CN=4VRS Gateway"));TRY(mbedtls_x509write_crt_set_issuer_name(&writer,"CN=4VRS Gateway"));
 t-=300;if(!gmtime_r(&t,&tm)||!strftime(before,sizeof(before),"%Y%m%d%H%M%S",&tm))goto fail;t+=86400*180;if(!gmtime_r(&t,&tm)||!strftime(after,sizeof(after),"%Y%m%d%H%M%S",&tm))goto fail;TRY(mbedtls_x509write_crt_set_validity(&writer,before,after));TRY(mbedtls_x509write_crt_set_basic_constraints(&writer,0,-1));TRY(mbedtls_x509write_crt_set_key_usage(&writer,MBEDTLS_X509_KU_DIGITAL_SIGNATURE));memset(san,0,sizeof(san));for(i=0;i<2;++i){const char *ip=i?ip1:ip0;if(!strcmp(ip,"-"))continue;if(inet_pton(AF_INET,ip,ips[count])!=1)goto fail;san[count].node.type=MBEDTLS_X509_SAN_IP_ADDRESS;san[count].node.san.unstructured_name.p=ips[count];san[count].node.san.unstructured_name.len=4;if(count)san[count-1].next=&san[count];++count;}if(!count)goto fail;TRY(mbedtls_x509write_crt_set_subject_alternative_name(&writer,san));TRY(mbedtls_x509write_crt_pem(&writer,pem,sizeof(pem),tls_random,&c->random));TRY(mbedtls_x509_crt_parse(&c->cert,pem,strlen((char*)pem)+1));TRY(mbedtls_pk_write_key_pem(&c->key,keypem,sizeof(keypem)));r=snprintf(record,sizeof(record),"4VRS_TLS_1\n%s\n%s%s",names,keypem,pem);if(r<0||(size_t)r>=sizeof(record))goto fail;category=24;TRY(web_atomic_file(path,record,(size_t)r));
ready:category=27;TRY(mbedtls_ssl_config_defaults(&c->config,MBEDTLS_SSL_IS_SERVER,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT));mbedtls_ssl_conf_rng(&c->config,tls_random,&c->random);TRY(mbedtls_ssl_conf_own_cert(&c->config,&c->cert,&c->key));r=0;goto done;
fail:r=-1;
done:
#ifdef WEB_HOST_TEST
 fprintf(stderr,"rng-certificate-end existing=%d result=%d\n",existing,r);
#endif
 web_clear(stored,sizeof(stored));web_clear(record,sizeof(record));web_clear(keypem,sizeof(keypem));mbedtls_mpi_free(&serial);mbedtls_x509write_crt_free(&writer);return r?category:0;
}
