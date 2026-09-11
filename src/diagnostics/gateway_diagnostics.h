#ifndef FOURVRS_GATEWAY_DIAGNOSTICS_H
#define FOURVRS_GATEWAY_DIAGNOSTICS_H

#include <stddef.h>
#include "app/gateway_application.h"

#define GATEWAY_DIAGNOSTIC_LINE_MAX 192U
#define GATEWAY_DIAGNOSTIC_SUMMARY_MAX 4096U
#define GATEWAY_DIAGNOSTIC_PORT_MAX 2048U
#define GATEWAY_DIAGNOSTIC_STARTUP_MAX 4096U
#define GATEWAY_DIAGNOSTIC_SUPPORT_MAX 8192U

typedef enum gateway_render_result { GATEWAY_RENDER_OK=0, GATEWAY_RENDER_TRUNCATED=1, GATEWAY_RENDER_INVALID=-1 } gateway_render_result_t;

typedef struct gateway_diagnostic_snapshot {
    gateway_application_health_t application;
    unsigned int schema_version;
    unsigned int startup_event_count;
    gateway_startup_event_t startup_events[GATEWAY_STARTUP_EVENT_CAPACITY];
    unsigned int queue_high_water;
    uart_backend_stats_t uart[GATEWAY_PORT_COUNT];
} gateway_diagnostic_snapshot_t;

int gateway_diagnostics_capture(const gateway_application_t *application,
                                gateway_diagnostic_snapshot_t *snapshot);
gateway_render_result_t gateway_diagnostics_render_summary(const gateway_diagnostic_snapshot_t*,char*,size_t,size_t*);
gateway_render_result_t gateway_diagnostics_render_port(const gateway_diagnostic_snapshot_t*,unsigned int,char*,size_t,size_t*);
gateway_render_result_t gateway_diagnostics_render_configuration(const gateway_diagnostic_snapshot_t*,char*,size_t,size_t*);
gateway_render_result_t gateway_diagnostics_render_system(const gateway_diagnostic_snapshot_t*,char*,size_t,size_t*);
gateway_render_result_t gateway_diagnostics_render_startup(const gateway_diagnostic_snapshot_t*,char*,size_t,size_t*);
gateway_render_result_t gateway_diagnostics_render_support(const gateway_diagnostic_snapshot_t*,char*,size_t,size_t*);
unsigned int gateway_diagnostic_snapshot_bytes(void);

#endif
