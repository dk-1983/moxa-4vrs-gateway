#ifndef FOURVRS_WEB_HTTP_H
#define FOURVRS_WEB_HTTP_H
#include "web/web_protocol.h"
#define WEB_HTTP_MAX 8192U
typedef struct web_http {char method[8],path[64],session[65],csrf[65],etag[256];const char *body;size_t length;unsigned int write,close;} web_http_t;
/* 0 incomplete; 1 parsed; negative HTTP status. authority is accepted local IP[:port]. */
int web_http_parse(char *,size_t,const char *,int,web_http_t *);
int web_http_ipc(const web_http_t *,char *,size_t);
int web_http_etag_match(const char *,const char *);
#endif
