#ifndef FOURVRS_WEB_PROTOCOL_H
#define FOURVRS_WEB_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define WEB_BODY_MAX 4096U
#define WEB_FRAME_MAX (WEB_BODY_MAX+8U)
typedef struct web_frame { unsigned char bytes[WEB_FRAME_MAX]; size_t used,total,sent; uint32_t since; } web_frame_t;
/* 4V + version(1) + reserved(0) + big endian body size. */
int web_frame_feed(web_frame_t *,const void *,size_t);
int web_frame_make(web_frame_t *,const char *);
uint32_t web_now(void);
int web_nonblock(int);
int web_random(void *,size_t);
void web_hex(const unsigned char *,size_t,char *);
int web_unhex(const char *,unsigned char *,size_t);
void web_clear(void *,size_t);
int web_equal(const void *,const void *,size_t);
int web_read_file(const char *,void *,size_t,size_t *);
int web_atomic_file(const char *,const void *,size_t);
/* Strict key=value LF body. No duplicates, unknown keys checked by caller;
 * UTF-8 values are percent encoded, decoded to bounded buffers. */
typedef struct web_fields { char key[32][32],value[32][256];unsigned int count; } web_fields_t;
int web_fields_parse(web_fields_t *,const char *,size_t);
const char *web_field(const web_fields_t *,const char *);
int web_fields_only(const web_fields_t *,const char *);
int web_number(const char *,unsigned int,unsigned int *);
#endif
