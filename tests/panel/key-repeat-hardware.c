#define _POSIX_C_SOURCE 200809L
#define main unit_test_main
#include "test_gateway_panel.c"
#undef main
#include <time.h>
#include <unistd.h>
#include <signal.h>
static const gateway_panel_ops_t *real_ops;
static int safe_poll(void *x,unsigned int *k){int r=real_ops->keypad_poll(x,k);if(r>0&&*k!=1U&&*k!=3U)return 0;return r;}
int main(void){
 gateway_application_t a;gateway_panel_t p;gateway_panel_moxa_t m;gateway_panel_ops_t hw;
 struct timespec started,current,delay={0,1000000};unsigned int elapsed=0,last=0,mask=0,lastmask=99;
 setvbuf(stdout,0,_IONBF,0);alarm(95);app_fixture(&a,GATEWAY_APP_READY);
 real_ops=gateway_panel_moxa_ops();hw=*real_ops;hw.keypad_poll=safe_poll;
 gateway_panel_moxa_context_init(&m);gateway_panel_init(&p,&hw,&m);p.key_state=gateway_panel_moxa_key_state;
 if(!m.key_register){gateway_panel_shutdown(&p);puts("NO_PHYSICAL_STATE");return 1;}
 p.view=GATEWAY_PANEL_EDIT_FIELD;p.field_index=10;p.candidate=a.coordinator.selected;p.candidate.ports[0].endpoint_port=502;
 clock_gettime(CLOCK_MONOTONIC,&started);puts("READY: temporary editor, no settings saved, F2/F4 only, 90 seconds");
 while(elapsed<90000){clock_gettime(CLOCK_MONOTONIC,&current);elapsed=(unsigned int)((current.tv_sec-started.tv_sec)*1000+(current.tv_nsec-started.tv_nsec)/1000000);test_now=elapsed;
 gateway_panel_step(&p,&a);gateway_panel_moxa_key_state(&m,&mask);
 if(last!=p.candidate.ports[0].endpoint_port||mask!=lastmask){last=p.candidate.ports[0].endpoint_port;lastmask=mask;printf("%u,%u,%u\n",elapsed,mask,last);}
 nanosleep(&delay,0);}
 gateway_panel_shutdown(&p);puts("FINISHED");return 0;
}
