#ifndef FOURVRS_WEB_SECURITY_H
#define FOURVRS_WEB_SECURITY_H
#include "web/web_protocol.h"
#define WEB_CODE_MS 180000U
#define WEB_SESSION_IDLE_MS 900000U
#define WEB_SESSION_MAX_MS 28800000U
#define WEB_PASSWORD_ROUNDS 600000U
typedef struct web_security {
 char path[512], code[13], token[65], csrf[65];
 unsigned char salt[16],hash[32],pending_salt[16];
 unsigned int administrator,recovery,attempts,login_attempts;
 uint32_t code_at,session_at,touched,rate_at,worker_at;
 int worker,worker_fd,worker_enroll,save_failed;
 unsigned int commit_pending;
} web_security_t;
int web_security_init(web_security_t *,const char *);
int web_security_local(web_security_t *,unsigned int,uint32_t);
void web_security_cancel_pending(web_security_t *);
int web_security_change(web_security_t *,const char *,const char *,const char *,uint32_t);
void web_security_cancel(web_security_t *);
void web_security_tick(web_security_t *,uint32_t);
int web_security_begin(web_security_t *,int,const char *,const char *,uint32_t);
/* 0 pending, HTTP status when complete. Never publishes a session before fsync. */
int web_security_poll(web_security_t *,uint32_t);
int web_security_authorized(web_security_t *,const char *,const char *,int,uint32_t);
#endif
