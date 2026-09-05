#ifndef FOURVRS_GATEWAY_CONSOLE_H
#define FOURVRS_GATEWAY_CONSOLE_H
#include <stddef.h>
#include "diagnostics/gateway_diagnostics.h"

typedef struct gateway_console_ops { int(*write)(void*,const char*,size_t);int(*read_key)(void*);int(*flush)(void*); } gateway_console_ops_t;
typedef enum gateway_console_view { GATEWAY_CONSOLE_MENU=0,GATEWAY_CONSOLE_SUMMARY,GATEWAY_CONSOLE_PORTS,GATEWAY_CONSOLE_CONFIG,GATEWAY_CONSOLE_SYSTEM,GATEWAY_CONSOLE_STARTUP,GATEWAY_CONSOLE_PORT_DETAIL } gateway_console_view_t;
typedef struct gateway_console { const gateway_console_ops_t*ops;void*context;gateway_console_view_t view;unsigned int selected_port;unsigned int rendered;unsigned int exit_requested;char output[GATEWAY_DIAGNOSTIC_SUPPORT_MAX]; } gateway_console_t;
int gateway_console_init(gateway_console_t*,const gateway_console_ops_t*,void*);
int gateway_console_step(gateway_console_t*,gateway_application_t*);
const gateway_console_ops_t *gateway_console_stdio_ops(void);
unsigned int gateway_console_memory_bytes(void);
#endif
