#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include "console/gateway_console.h"
#include "version.h"

static int emit(gateway_console_t*c,const char*s,size_t n){if(c->ops->write(c->context,s,n)!=0)return-1;return c->ops->flush?c->ops->flush(c->context):0;}
static int menu(gateway_console_t*c){int n=snprintf(c->output,sizeof(c->output),"\n%s %s\n1. Gateway status\n2. Ports\n3. Configuration status\n4. System information\n5. Shutdown / exit menu\nE. Startup events\nD. Support dump\n> ",FOURVRS_PRODUCT_NAME,FOURVRS_VERSION);return n>0&&(size_t)n<sizeof(c->output)?emit(c,c->output,(size_t)n):-1;}
int gateway_console_init(gateway_console_t*c,const gateway_console_ops_t*o,void*x){if(!c||!o||!o->write||!o->read_key)return-1;memset(c,0,sizeof(*c));c->ops=o;c->context=x;return 0;}
static int render(gateway_console_t*c,gateway_application_t*a){gateway_diagnostic_snapshot_t s;size_t n=0;gateway_render_result_t r;if(gateway_diagnostics_capture(a,&s)!=0)return-1;if(c->view==GATEWAY_CONSOLE_MENU)return menu(c);if(c->view==GATEWAY_CONSOLE_SUMMARY||c->view==GATEWAY_CONSOLE_PORTS)r=gateway_diagnostics_render_summary(&s,c->output,sizeof(c->output),&n);else if(c->view==GATEWAY_CONSOLE_CONFIG)r=gateway_diagnostics_render_configuration(&s,c->output,sizeof(c->output),&n);else if(c->view==GATEWAY_CONSOLE_SYSTEM)r=gateway_diagnostics_render_system(&s,c->output,sizeof(c->output),&n);else if(c->view==GATEWAY_CONSOLE_STARTUP)r=gateway_diagnostics_render_startup(&s,c->output,sizeof(c->output),&n);else r=gateway_diagnostics_render_port(&s,c->selected_port,c->output,sizeof(c->output),&n);if(r==GATEWAY_RENDER_INVALID)return-1;if(emit(c,c->output,n)!=0)return-1;return emit(c,"\nB. Back\n> ",12U);}
int gateway_console_step(gateway_console_t*c,gateway_application_t*a){int k;if(!c||!a)return-1;if(!c->rendered){c->rendered=1;return render(c,a);}k=c->ops->read_key(c->context);if(k<0)return 0;if(k=='b'||k=='B'){c->view=GATEWAY_CONSOLE_MENU;c->rendered=0;return 0;}if(c->view==GATEWAY_CONSOLE_MENU){if(k=='1')c->view=GATEWAY_CONSOLE_SUMMARY;else if(k=='2')c->view=GATEWAY_CONSOLE_PORTS;else if(k=='3')c->view=GATEWAY_CONSOLE_CONFIG;else if(k=='4')c->view=GATEWAY_CONSOLE_SYSTEM;else if(k=='5'){c->exit_requested=1;gateway_application_request_stop(a);return 0;}else if(k=='e'||k=='E')c->view=GATEWAY_CONSOLE_STARTUP;else if(k=='d'||k=='D'){gateway_diagnostic_snapshot_t s;size_t n;if(gateway_diagnostics_capture(a,&s)!=0)return-1;if(gateway_diagnostics_render_support(&s,c->output,sizeof(c->output),&n)==GATEWAY_RENDER_INVALID)return-1;return emit(c,c->output,n);}else return 0;}else if(c->view==GATEWAY_CONSOLE_PORTS&&k>='1'&&k<='8'){c->selected_port=(unsigned int)(k-'1');c->view=GATEWAY_CONSOLE_PORT_DETAIL;}else return 0;c->rendered=0;return 0;}
static int std_write(void*x,const char*s,size_t n){FILE*f=x?(FILE*)x:stdout;return fwrite(s,1,n,f)==n?0:-1;}
static int std_read(void*x){fd_set fds;struct timeval timeout;unsigned char key;(void)x;FD_ZERO(&fds);FD_SET(STDIN_FILENO,&fds);timeout.tv_sec=0;timeout.tv_usec=0;if(select(STDIN_FILENO+1,&fds,0,0,&timeout)<=0)return-1;return read(STDIN_FILENO,&key,1U)==1?key:-1;}
static int std_flush(void*x){return fflush(x?(FILE*)x:stdout);}
static const gateway_console_ops_t std_ops={std_write,std_read,std_flush};
const gateway_console_ops_t*gateway_console_stdio_ops(void){return &std_ops;}
unsigned int gateway_console_memory_bytes(void){return(unsigned int)sizeof(gateway_console_t);}
