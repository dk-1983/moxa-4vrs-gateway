#ifndef FOURVRS_GATEWAY_AUTOSTART_H
#define FOURVRS_GATEWAY_AUTOSTART_H
#define GATEWAY_INSTALL_ROOT "/var/hda/4vrs"
#define GATEWAY_BINARY_PATH GATEWAY_INSTALL_ROOT "/bin/4vrs-gateway"
#define GATEWAY_DISABLE_PATH GATEWAY_INSTALL_ROOT "/disable-autostart"
#define GATEWAY_PID_PATH GATEWAY_INSTALL_ROOT "/run/4vrs-gateway.pid"
#define GATEWAY_LOG_PATH GATEWAY_INSTALL_ROOT "/log/startup.log"
typedef enum gateway_autostart_result { GATEWAY_AUTOSTART_START=0,GATEWAY_AUTOSTART_DISABLED,GATEWAY_AUTOSTART_STORAGE_UNAVAILABLE,GATEWAY_AUTOSTART_STORAGE_READ_ONLY,GATEWAY_AUTOSTART_BINARY_MISSING,GATEWAY_AUTOSTART_BINARY_NOT_EXECUTABLE,GATEWAY_AUTOSTART_ALREADY_RUNNING,GATEWAY_AUTOSTART_STALE_PID } gateway_autostart_result_t;
typedef struct gateway_autostart_ops { int(*exists)(void*,const char*);int(*writable)(void*,const char*);int(*executable)(void*,const char*);int(*pid_read)(void*,const char*,unsigned long*);int(*pid_matches)(void*,unsigned long,const char*); } gateway_autostart_ops_t;
gateway_autostart_result_t gateway_autostart_check(const gateway_autostart_ops_t*,void*);
#endif
