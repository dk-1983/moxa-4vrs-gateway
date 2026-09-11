#ifndef FOURVRS_GATEWAY_STARTUP_PRESENTATION_H
#define FOURVRS_GATEWAY_STARTUP_PRESENTATION_H
#include <stddef.h>
#include "app/gateway_application.h"
#define GATEWAY_SPLASH_MAX 1024U
#define GATEWAY_SPLASH_BAR_WIDTH 20U
typedef enum gateway_splash_charset { GATEWAY_SPLASH_ASCII=0, GATEWAY_SPLASH_UTF8=1 } gateway_splash_charset_t;
typedef struct gateway_startup_presentation { unsigned int last_sequence;unsigned int last_percent;unsigned int renders;gateway_splash_charset_t charset;char output[GATEWAY_SPLASH_MAX]; } gateway_startup_presentation_t;
void gateway_startup_presentation_init(gateway_startup_presentation_t*,gateway_splash_charset_t);
int gateway_startup_presentation_render(gateway_startup_presentation_t*,const gateway_application_t*,size_t*);
unsigned int gateway_startup_presentation_bytes(void);
#endif
