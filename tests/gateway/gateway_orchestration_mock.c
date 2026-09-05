#include <string.h>
#include "gateway/gateway_orchestration_mock.h"

static int start(void *context,const gateway_port_config_t*config)
{gateway_transport_mock_t*m=(gateway_transport_mock_t*)context;++m->starts;if(m->start_failures_remaining>0){--m->start_failures_remaining;return-1;}m->active=1;m->endpoint=config->endpoint_port;return 0;}
static int stop(void *context)
{gateway_transport_mock_t*m=(gateway_transport_mock_t*)context;++m->stops;m->active=0;m->clients=0;if(m->stop_failures_remaining>0){--m->stop_failures_remaining;return-1;}return 0;}
static unsigned int clients(const void *context)
{return ((const gateway_transport_mock_t*)context)->clients;}
static unsigned int crc_errors(const void *context)
{return ((const gateway_transport_mock_t*)context)->crc_error_count;}
static unsigned int framing_errors(const void *context)
{return ((const gateway_transport_mock_t*)context)->framing_error_count;}
static void step(void *context,core_tick_t now){(void)context;(void)now;}
static void diagnostics(const void*c,gateway_transport_diagnostics_t*d){const gateway_transport_mock_t*m=(const gateway_transport_mock_t*)c;memset(d,0,sizeof(*d));d->clients=m->clients;d->crc_failures=m->crc_error_count;d->framing_failures=m->framing_error_count;}
static const gateway_transport_ops_t ops={start,step,stop,clients,crc_errors,framing_errors,diagnostics};
void gateway_transport_mock_init(gateway_transport_mock_t*m){memset(m,0,sizeof(*m));}
const gateway_transport_ops_t *gateway_transport_mock_ops(void){return &ops;}
