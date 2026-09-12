/* Local feasibility executable. No Gateway IPC, credentials, or mutations.
 * --serve binds ONLY 127.0.0.1:18443. Never deployed by the installer. */
#define _POSIX_C_SOURCE 200112L
#include "core/monotonic.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

static union { unsigned char bytes[512U * 1024U]; uint64_t alignment; } arena;
static int entropy_failed;
static unsigned int entropy_calls;
static int entropy(void *context, unsigned char *out, size_t n, size_t *used)
{
    int fd, saved; ssize_t got; size_t wanted = n > 32U ? 32U : n;
    (void)context; *used = 0;
    ++entropy_calls;
    if (entropy_failed) { fputs("stage=entropy reason=injected_failure errno=0\n", stderr); return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED; }
    fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { saved=errno; fprintf(stderr,"stage=entropy reason=open errno=%d requested=%lu\n",saved,(unsigned long)wanted); return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED; }
    got = read(fd, out, wanted); saved = got < 0 ? errno : 0;
    close(fd);
    fprintf(stderr,"stage=entropy source=/dev/random nonblock=1 call=%u requested=%lu received=%ld errno=%d reason=%s\n",entropy_calls,(unsigned long)wanted,(long)got,saved,got<0?(saved==EAGAIN?"would_block":saved==EINTR?"interrupted":"read_error"):got==0?"eof":(size_t)got!=wanted?"short_read":"full_read");
    /* Match production's exact-read policy. Never retry or fall back. */
    if (got != (ssize_t)wanted) { volatile unsigned char *p=out; size_t i; for(i=0;i<wanted;i++)p[i]=0; return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED; }
    *used = (size_t)got;
    return 0;
}
static unsigned long now_ms(void)
{
    struct timespec t;
    if (gateway_monotonic_time( &t)) exit(2);
    return (unsigned long)t.tv_sec * 1000UL + (unsigned long)t.tv_nsec / 1000000UL;
}
static int send_bytes(void *p, const unsigned char *b, size_t n)
{
    ssize_t r = send(*(int *)p, b, n, 0);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)r;
}
static int recv_bytes(void *p, unsigned char *b, size_t n)
{
    ssize_t r = recv(*(int *)p, b, n, 0);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return MBEDTLS_ERR_SSL_WANT_READ;
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)r;
}
static int wait_socket(int fd, int writing, unsigned long start)
{
    fd_set set; struct timeval t;
    if (now_ms() - start >= 3000UL) return -1;
    FD_ZERO(&set); FD_SET(fd, &set); t.tv_sec = 0; t.tv_usec = 10000;
    if (select(fd + 1, writing ? NULL : &set, writing ? &set : NULL, NULL, &t) < 0 && errno != EINTR) return -1;
    return 0;
}
static int serve(mbedtls_ssl_config *cfg)
{
    int listener = -1, client = -1, r = -1, flags;
    struct sockaddr_in address;
    unsigned long start;
    mbedtls_ssl_context ssl;
    char request[2049]; size_t used = 0, sent = 0;
    const char response[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 22\r\nConnection: close\r\n\r\n{\"foundation\":\"local\"}";
    mbedtls_ssl_init(&ssl);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0 || listener >= FD_SETSIZE) goto done;
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_port = htons(18443); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    flags = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1) || fcntl(listener, F_SETFL, O_NONBLOCK)) goto done;
    puts("READY 127.0.0.1:18443"); fflush(stdout); start = now_ms();
    while ((client = accept(listener, NULL, NULL)) < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) goto done;
        if (wait_socket(listener, 0, start)) goto done;
    }
    close(listener); listener = -1; /* one client, no accept backlog during work */
    if (client >= FD_SETSIZE || fcntl(client, F_SETFL, O_NONBLOCK)) goto done;
    if (mbedtls_ssl_setup(&ssl, cfg)) goto done;
    mbedtls_ssl_set_bio(&ssl, &client, send_bytes, recv_bytes, NULL);
    start = now_ms();
    do {
        r = mbedtls_ssl_handshake(&ssl);
        if (r && r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) goto done;
        if (r && wait_socket(client, r == MBEDTLS_ERR_SSL_WANT_WRITE, start)) { r = -1; goto done; }
    } while (r);
    request[0] = 0;
    for (;;) {
        if (used == sizeof(request) - 1U) { r = -1; goto done; }
        r = mbedtls_ssl_read(&ssl, (unsigned char *)request + used, sizeof(request) - 1U - used);
        if (r > 0) {
            used += (size_t)r; request[used] = 0;
            if (memchr(request, 0, used)) { r = -1; goto done; }
            if (strstr(request, "\r\n\r\n")) break;
        } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) { r = -1; goto done; }
        if (wait_socket(client, r == MBEDTLS_ERR_SSL_WANT_WRITE, start)) { r = -1; goto done; }
    }
    /* Exact fixture request only; no general HTTP parser exposed. */
    if (strcmp(request, "GET /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")) { r = -1; goto done; }
    while (sent < sizeof(response) - 1U) {
        r = mbedtls_ssl_write(&ssl, (const unsigned char *)response + sent, sizeof(response) - 1U - sent);
        if (r > 0) sent += (size_t)r;
        else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) goto done;
        if (wait_socket(client, r == MBEDTLS_ERR_SSL_WANT_WRITE, start)) { r = -1; goto done; }
    }
    r = 0;
done:
    mbedtls_ssl_free(&ssl);
    if (client >= 0) close(client);
    if (listener >= 0) close(listener);
    return r;
}
#define CHECK(call) do { if ((r = (call)) != 0) { fprintf(stderr, "stage=certificate operation=%s line=%d result=%d\n", #call, __LINE__, r); goto done; } } while (0)
int main(int argc, char **argv)
{
    mbedtls_entropy_context ent;
    mbedtls_ctr_drbg_context rng;
    mbedtls_pk_context key;
    mbedtls_x509write_cert writer;
    mbedtls_x509_crt cert;
    mbedtls_ssl_config cfg;
    mbedtls_mpi serial;
    mbedtls_x509_san_list san;
    unsigned char pem[4096], digest[32], serial_bytes[16], ip[4] = {127,0,0,1};
    char before[16], after[16]; struct tm tm; time_t t = time(NULL);
    int r = 1; FILE *f;
    if (argc != 3 || (strcmp(argv[1], "--serve") && strcmp(argv[1], "--selftest") && strcmp(argv[1], "--entropy-fail") && strcmp(argv[1], "--unsynced"))) return 2;
    if (!strcmp(argv[1], "--unsynced") || t < 1767225600 || t > 2114380800) { fputs("clock unsynced: TLS disabled\n", stderr); return 3; }
    signal(SIGPIPE, SIG_IGN);
    entropy_failed = !strcmp(argv[1], "--entropy-fail");
    entropy_calls = 0;
    mbedtls_memory_buffer_alloc_init(arena.bytes, sizeof(arena.bytes));
    mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&rng); mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&writer); mbedtls_x509_crt_init(&cert);
    mbedtls_ssl_config_init(&cfg); mbedtls_mpi_init(&serial);
    CHECK(mbedtls_entropy_add_source(&ent, entropy, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG));
    CHECK(mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &ent, (const unsigned char *)"4vrs-web-probe", 14));
    CHECK(mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)));
    CHECK(mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &rng));
    CHECK(mbedtls_ctr_drbg_random(&rng, serial_bytes, sizeof(serial_bytes)));
    serial_bytes[0] &= 0x7f; serial_bytes[0] |= 1;
    CHECK(mbedtls_mpi_read_binary(&serial, serial_bytes, sizeof(serial_bytes)));
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &key); mbedtls_x509write_crt_set_issuer_key(&writer, &key);
    CHECK(mbedtls_x509write_crt_set_serial(&writer, &serial));
    CHECK(mbedtls_x509write_crt_set_subject_name(&writer, "CN=4VRS local feasibility"));
    CHECK(mbedtls_x509write_crt_set_issuer_name(&writer, "CN=4VRS local feasibility"));
    t -= 300; if (!gmtime_r(&t, &tm) || !strftime(before, sizeof(before), "%Y%m%d%H%M%S", &tm)) goto done;
    t += 86400 * 30; if (!gmtime_r(&t, &tm) || !strftime(after, sizeof(after), "%Y%m%d%H%M%S", &tm)) goto done;
    CHECK(mbedtls_x509write_crt_set_validity(&writer, before, after));
    CHECK(mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1));
    CHECK(mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE));
    memset(&san, 0, sizeof(san)); san.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
    san.node.san.unstructured_name.p = ip; san.node.san.unstructured_name.len = sizeof(ip);
    CHECK(mbedtls_x509write_crt_set_subject_alternative_name(&writer, &san));
    CHECK(mbedtls_x509write_crt_pem(&writer, pem, sizeof(pem), mbedtls_ctr_drbg_random, &rng));
    CHECK(mbedtls_x509_crt_parse(&cert, pem, strlen((char *)pem) + 1));
    f = fopen(argv[2], "wb"); if (!f) { r = -1; goto done; }
    if (fwrite(pem, 1, strlen((char *)pem), f) != strlen((char *)pem)) { fclose(f); r = -1; goto done; }
    if (fclose(f)) { r = -1; goto done; }
    CHECK(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), serial_bytes, 16, pem, strlen((char *)pem), digest));
    CHECK(mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *)"password", 8,
          (const unsigned char *)"salt", 4, 1, sizeof(digest), digest));
    { const unsigned char expected[32]={0x12,0x0f,0xb6,0xcf,0xfc,0xf8,0xb3,0x2c,0x43,0xe7,0x22,0x52,0x56,0xc4,0xf8,0x37,0xa8,0x65,0x48,0xc9,0x2c,0xcc,0x35,0x48,0x08,0x05,0x98,0x7c,0xb7,0x0b,0xe1,0x7b};
      if(memcmp(digest,expected,sizeof(digest))) { r=-1;goto done; } }
    CHECK(mbedtls_ssl_config_defaults(&cfg, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT));
    mbedtls_ssl_conf_rng(&cfg, mbedtls_ctr_drbg_random, &rng);
    CHECK(mbedtls_ssl_conf_own_cert(&cfg, &cert, &key));
    if (!strcmp(argv[1], "--serve")) CHECK(serve(&cfg));
    puts("TLS/cert/P256/CTR-DRBG/HMAC-SHA256/PBKDF2-vector PASS; fixed crypto arena=524288"); r = 0;
done:
    mbedtls_ssl_config_free(&cfg); mbedtls_x509_crt_free(&cert);
    mbedtls_x509write_crt_free(&writer); mbedtls_pk_free(&key); mbedtls_mpi_free(&serial);
    mbedtls_ctr_drbg_free(&rng); mbedtls_entropy_free(&ent); mbedtls_memory_buffer_alloc_free();
    return r ? 1 : 0;
}
