#ifndef FOURVRS_KDF_WORKER_H
#define FOURVRS_KDF_WORKER_H
#include <stdint.h>
/* Local ABI wire consists only of bytes, no padding/native integers/secrets in argv. */
#define KDF_WIRE_SIZE 328
#define KDF_COMMIT_SIZE 568
void web_security_worker_path(const char *);
#endif
