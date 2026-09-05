#include <string.h>
#include "network/echo_rtu_backend.h"

static int bopen(void *p,core_tick_t n){(void)p;(void)n;return 0;}
static int bstart(void *p,const core_transaction_t *t,core_tick_t n)
{
    echo_rtu_backend_t *b=(echo_rtu_backend_t *)p;(void)n;
    ++b->starts;
    b->active_generation=t->generation;b->transmit_pending=1;
    if(b->suppress_responses!=0U){--b->suppress_responses;b->pending=0;return 0;}
    memset(&b->event,0,sizeof(b->event));b->event.type=BACKEND_EVENT_RESPONSE;
    b->event.generation=t->generation;b->event.length=t->payload_length;
    memcpy(b->event.data,t->payload,t->payload_length);b->pending=1;return 0;
}
static int bpoll(void *p,core_tick_t n,backend_event_t *e)
{echo_rtu_backend_t*b=(echo_rtu_backend_t*)p;(void)n;if(b->transmit_pending){memset(e,0,sizeof(*e));e->type=BACKEND_EVENT_TRANSMITTED;e->generation=b->active_generation;b->transmit_pending=0;return 1;}if(!b->pending)return 0;*e=b->event;b->pending=0;return 1;}
static void bcancel(void*p,unsigned int g){echo_rtu_backend_t*b=(echo_rtu_backend_t*)p;(void)g;b->pending=0;b->transmit_pending=0;}
static int bok(void*p,core_tick_t n){(void)p;(void)n;return 0;}
static int breconfig(void*p,const core_port_config_t*c,core_tick_t n){(void)p;(void)c;(void)n;return 0;}
static const core_backend_ops_t ops={bopen,bstart,bpoll,bcancel,bok,breconfig,bok};
void echo_rtu_backend_init(echo_rtu_backend_t *b){memset(b,0,sizeof(*b));}
const core_backend_ops_t *echo_rtu_backend_ops(void){return &ops;}
void echo_rtu_backend_suppress(echo_rtu_backend_t *b,unsigned int n)
{if(b!=0)b->suppress_responses=n;}
