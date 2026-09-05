#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "app/gateway_application.h"
#include "network/gateway_network_boot.h"
#include "console/gateway_console.h"
#include "launcher/gateway_startup_presentation.h"
#include "panel/gateway_panel.h"
#include "panel/gateway_panel_moxa.h"

static gateway_run_control_t *active_control;
static gateway_application_t application;
static gateway_console_t console;
static gateway_startup_presentation_t presentation;
static gateway_panel_t panel;
static gateway_panel_moxa_t panel_moxa;
static void request_stop(int n){(void)n;if(active_control)active_control->stop_requested=1;}
static int install_signals(void){struct sigaction a;a.sa_handler=request_stop;sigemptyset(&a.sa_mask);a.sa_flags=0;return sigaction(SIGINT,&a,0)==0&&sigaction(SIGTERM,&a,0)==0?0:-1;}
static const char*source_name(gateway_config_source_t s){switch(s){case GATEWAY_CONFIG_SOURCE_ACTIVE:return"active";case GATEWAY_CONFIG_SOURCE_BACKUP:return"backup";case GATEWAY_CONFIG_SOURCE_DEFAULTS:return"defaults";default:return"safe-mode";}}
static const char*state_name(gateway_application_state_t s){switch(s){case GATEWAY_APP_READY:return"READY";case GATEWAY_APP_DEGRADED:return"DEGRADED";case GATEWAY_APP_SAFE_MODE:return"SAFE_MODE";case GATEWAY_APP_FATAL_ERROR:return"FATAL_ERROR";default:return"STARTING";}}
static void present_event(void){size_t n;gateway_startup_event_t*e;if(!application.coordinator.event_count)return;e=&application.coordinator.events[(application.coordinator.event_head+application.coordinator.event_count-1U)%GATEWAY_STARTUP_EVENT_CAPACITY];if(e->sequence!=presentation.last_sequence&&gateway_startup_presentation_render(&presentation,&application,&n)==0){fwrite(presentation.output,1,n,stdout);fflush(stdout);}}
int main(int argc,char**argv){gateway_application_state_t announced=GATEWAY_APP_BOOTSTRAP;const char*directory=GATEWAY_CONFIG_TARGET_DIRECTORY;int console_ready=0;if(argc==2&&!strcmp(argv[1],"--network-boot")){const char *stage=0;int result=gateway_network_boot_restore_detailed(gateway_network_environment_production(),&stage);fprintf(stderr,"network-boot %s stage=%s\n",result?"failed":"ok",stage?stage:"unknown");return result?1:0;}
if(argc==2&&!strcmp(argv[1],"--network-vendor-up"))return gateway_network_vendor_boot("/etc/4vrs-network",1)?1:0;
if(argc==2&&!strcmp(argv[1],"--network-vendor-down"))return gateway_network_vendor_boot("/etc/4vrs-network",0)?1:0;
if(argc==2&&!strcmp(argv[1],"--network-owner-start"))return gateway_network_boot_service()?1:0;
if(argc==3&&!strcmp(argv[1],"--network-enroll")){
 gateway_persistent_config_t configuration;gateway_config_source_t source;const char *bindings[8],*stage;unsigned int i;int result;
 if(gateway_config_startup_select(argv[2],&configuration,&source)!=GATEWAY_CONFIG_OK||source==GATEWAY_CONFIG_SOURCE_SAFE_MODE){fprintf(stderr,"network-enroll failed stage=configuration\n");return 1;}
 for(i=0;i<8U;++i)bindings[i]=configuration.ports[i].bind_address;
 result=gateway_network_enroll_detailed(gateway_network_environment_production(),bindings,&stage);
 fprintf(stderr,"network-enroll %s stage=%s\n",result?"failed":"ok",stage);return result?1:0;
}
if(argc>2){return GATEWAY_EXIT_INVALID_INVOCATION;}if(argc==2)directory=argv[1];if(gateway_application_init_production(&application,directory)!=0)return GATEWAY_EXIT_FATAL_STARTUP;gateway_panel_moxa_context_init(&panel_moxa);gateway_panel_init(&panel,gateway_panel_moxa_ops(),&panel_moxa);gateway_startup_presentation_init(&presentation,GATEWAY_SPLASH_ASCII);active_control=&application.run_control;if(install_signals()!=0){gateway_application_request_stop(&application);while(!gateway_application_finished(&application)){gateway_application_step(&application);gateway_panel_step(&panel,&application);}gateway_panel_shutdown(&panel);return GATEWAY_EXIT_FATAL_STARTUP;}printf("product=%s version=%s state=starting\n",FOURVRS_PRODUCT_NAME,FOURVRS_VERSION);fflush(stdout);while(!gateway_application_finished(&application)){gateway_application_step(&application);gateway_panel_step(&panel,&application);present_event();if(application.coordinator.state!=announced&&(application.coordinator.state==GATEWAY_APP_READY||application.coordinator.state==GATEWAY_APP_DEGRADED||application.coordinator.state==GATEWAY_APP_SAFE_MODE||application.coordinator.state==GATEWAY_APP_FATAL_ERROR)){announced=application.coordinator.state;printf("configuration_source=%s startup_result=%s\n",source_name(application.coordinator.config_source),state_name(announced));fflush(stdout);if(announced!=GATEWAY_APP_FATAL_ERROR&&gateway_console_init(&console,gateway_console_stdio_ops(),stdout)==0)console_ready=1;}if(console_ready&&gateway_console_step(&console,&application)!=0)console_ready=0;if(!gateway_application_finished(&application))application.dependencies.wait(application.dependencies.wait_context,GATEWAY_APPLICATION_WAIT_USEC);}gateway_panel_shutdown(&panel);active_control=0;return(int)gateway_application_exit_status(&application);}
