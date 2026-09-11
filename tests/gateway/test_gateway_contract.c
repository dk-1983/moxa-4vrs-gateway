#include <stdio.h>
#include <string.h>
#include "core/port_runtime.h"
#include "modbus/modbus_crc.h"
#include "modbus/modbus_tcp_adapter.h"

static unsigned int checks, failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line=%d %s\n",__LINE__,#x);}}while(0)

typedef struct scripted_backend { backend_event_t event; int pending; core_transaction_t seen; } scripted_backend_t;
typedef struct result { unsigned char mbap[260]; unsigned int length,status,count; } result_t;
static int bopen(void*c,core_tick_t n){(void)c;(void)n;return 0;}
static int bstart(void*c,const core_transaction_t*t,core_tick_t n){scripted_backend_t*b=c;(void)n;b->seen=*t;return 0;}
static int bpoll(void*c,core_tick_t n,backend_event_t*e){scripted_backend_t*b=c;(void)n;if(!b->pending)return 0;*e=b->event;b->pending=0;return 1;}
static void bcancel(void*c,unsigned int g){(void)c;(void)g;}
static int bok(void*c,core_tick_t n){(void)c;(void)n;return 0;}
static int breconf(void*c,const core_port_config_t*p,core_tick_t n){(void)c;(void)p;(void)n;return 0;}
static const core_backend_ops_t ops={bopen,bstart,bpoll,bcancel,bok,breconf,bok};
static void done(void*c,const core_transaction_t*t,core_transaction_status_t s,const unsigned char*r,unsigned int n)
{result_t*x=c;x->status=(unsigned int)s;++x->count;if(s==CORE_TX_COMPLETED)CHECK(modbus_tcp_build_response(t,r,n,x->mbap,sizeof(x->mbap),&x->length)==0);}
static core_port_config_t cfg(void){core_port_config_t c;memset(&c,0,sizeof(c));c.baud=115200;c.data_bits=8;c.stop_bits=1;c.endpoint_port=1502;return c;}
static void inject(scripted_backend_t*b,unsigned int generation,const unsigned char*p,unsigned int n)
{memset(&b->event,0,sizeof(b->event));b->event.type=BACKEND_EVENT_RESPONSE;b->event.generation=generation;b->event.length=n;memcpy(b->event.data,p,n);b->pending=1;}
int main(void)
{
    static const unsigned char expected_request[8]={8,4,0,0,0,40,0xF0,0x8D};
    unsigned char normal[85],exception[5]; unsigned short crc; unsigned int i;
    core_port_config_t c=cfg();port_runtime_t port;scripted_backend_t b;result_t result;
    modbus_tcp_request_t q;memset(&b,0,sizeof(b));memset(&result,0,sizeof(result));
    port_runtime_init(&port,0,&c,&ops,&b);port.state=PORT_READY;port_runtime_set_completion(&port,done,&result);
    memset(&q,0,sizeof(q));q.client_id=2;q.client_epoch=7;q.adu.transaction_id=0x1001;q.adu.unit_id=8;
    q.adu.pdu_length=5;q.adu.pdu[0]=4;q.adu.pdu[4]=40;
    CHECK(modbus_tcp_submit(&port,&q,0,100,100,0)==CORE_ACCEPTED);port_runtime_step(&port,0);port_runtime_step(&port,0);
    CHECK(memcmp(b.seen.payload,expected_request,8)==0);
    CHECK(b.seen.transport_metadata[0]==0x1001&&b.seen.transport_metadata[1]==0x0804);
    CHECK(b.seen.operation.direction==CORE_OPERATION_TX_THEN_RX&&b.seen.operation.response_policy==CORE_RESPONSE_BACKEND_FRAMED);
    memset(normal,0,sizeof(normal));normal[0]=8;normal[1]=4;normal[2]=80;for(i=0;i<80;++i)normal[3+i]=(unsigned char)i;
    crc=modbus_crc16(normal,83);normal[83]=(unsigned char)crc;normal[84]=(unsigned char)(crc>>8);
    CHECK(modbus_rtu_frame_probe(0,b.seen.payload,8,normal,85,&i)==UART_FRAME_COMPLETE&&i==85);
    inject(&b,b.seen.generation,normal,85);port_runtime_step(&port,1);
    CHECK(result.count==1&&result.status==CORE_TX_COMPLETED&&result.length==89);
    CHECK(result.mbap[0]==0x10&&result.mbap[1]==1&&result.mbap[6]==8&&result.mbap[7]==4);
    b.seen.transport_metadata[MODBUS_META_TRANSACTION_ID]=0x2001;
    b.seen.transport_metadata[MODBUS_META_UNIT_FUNCTION]=0x1804;
    CHECK(modbus_tcp_build_gateway_exception(&b.seen,MODBUS_EXCEPTION_GATEWAY_TARGET_NO_RESPONSE,result.mbap,sizeof(result.mbap),&i)==0);
    {static const unsigned char timeout_mbap[9]={0x20,0x01,0,0,0,3,24,0x84,0x0B};CHECK(i==9&&memcmp(result.mbap,timeout_mbap,9)==0);}
    CHECK(modbus_tcp_build_gateway_exception(&b.seen,MODBUS_EXCEPTION_GATEWAY_PATH_UNAVAILABLE,result.mbap,sizeof(result.mbap),&i)==0&&result.mbap[8]==0x0A);
    exception[0]=8;exception[1]=0x84;exception[2]=2;crc=modbus_crc16(exception,3);exception[3]=(unsigned char)crc;exception[4]=(unsigned char)(crc>>8);
    CHECK(modbus_rtu_frame_probe(0,expected_request,8,exception,5,&i)==UART_FRAME_COMPLETE&&i==5);
    exception[4]^=1;CHECK(modbus_tcp_build_response(&b.seen,exception,5,result.mbap,sizeof(result.mbap),&i)==-2);exception[4]^=1;
    CHECK(modbus_rtu_frame_probe(0,expected_request,8,normal,84,&i)==UART_FRAME_INCOMPLETE);
    normal[0]=9;CHECK(modbus_rtu_frame_probe(0,expected_request,8,normal,3,&i)==UART_FRAME_MALFORMED);normal[0]=8;
    normal[1]=3;CHECK(modbus_rtu_frame_probe(0,expected_request,8,normal,3,&i)==UART_FRAME_MALFORMED);
    printf("gateway_contract checks=%u failed=%u request_crc=%02X%02X normal_bytes=85 exception_bytes=5\n",checks,failures,expected_request[6],expected_request[7]);
    return failures?1:0;
}
