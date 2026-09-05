#include <stdio.h>
#include <string.h>

#include "core/mock_backend.h"
#include "modbus/mbap_stream.h"
#include "modbus/modbus_crc.h"
#include "modbus/modbus_dispatcher.h"

static unsigned long checks;
static unsigned long failures;

#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)

typedef struct capture {
    mbap_adu_t frames[16];
    unsigned int count;
} capture_t;

static int capture_frame(void *context, const mbap_adu_t *adu)
{
    capture_t *capture = (capture_t *)context;
    if (capture->count == 16U)
        return 1;
    capture->frames[capture->count++] = *adu;
    return 0;
}

static int capture_one(void *context, const mbap_adu_t *adu)
{
    (void)context;
    (void)adu;
    return 1;
}

static unsigned int make_mbap(unsigned char *out, unsigned short tid,
                              unsigned char unit, const unsigned char *pdu,
                              unsigned int pdu_length)
{
    unsigned int length = pdu_length + 1U;
    out[0] = (unsigned char)(tid >> 8); out[1] = (unsigned char)tid;
    out[2] = 0; out[3] = 0;
    out[4] = (unsigned char)(length >> 8); out[5] = (unsigned char)length;
    out[6] = unit;
    memcpy(out + 7, pdu, pdu_length);
    return 7U + pdu_length;
}

static void parser_fragmentation(void)
{
    unsigned char frame[MBAP_ADU_MAX];
    unsigned char pdu[64];
    unsigned int frame_length;
    unsigned int split;
    unsigned int i;
    mbap_stream_t stream;
    capture_t capture;
    for (i = 0; i < sizeof(pdu); ++i) pdu[i] = (unsigned char)(i + 1U);
    frame_length = make_mbap(frame, 0x1234U, 17, pdu, sizeof(pdu));
    for (split = 0; split <= frame_length; ++split) {
        mbap_stream_init(&stream); memset(&capture, 0, sizeof(capture));
        CHECK(mbap_stream_feed(&stream, frame, split, capture_frame, &capture) == MBAP_OK);
        CHECK(capture.count == (split == frame_length ? 1U : 0U));
        CHECK(mbap_stream_feed(&stream, frame + split, frame_length - split,
                               capture_frame, &capture) == MBAP_OK);
        CHECK(capture.count == 1U);
        CHECK(capture.frames[0].transaction_id == 0x1234U);
        CHECK(capture.frames[0].pdu_length == sizeof(pdu));
        CHECK(memcmp(capture.frames[0].pdu, pdu, sizeof(pdu)) == 0);
    }
    mbap_stream_init(&stream); memset(&capture, 0, sizeof(capture));
    for (i = 0; i < frame_length; ++i)
        CHECK(mbap_stream_feed(&stream, frame + i, 1, capture_frame, &capture) == MBAP_OK);
    CHECK(capture.count == 1U);
}

static void parser_coalescing(void)
{
    unsigned char chunk[800];
    unsigned char pdu[2] = { 3, 0 };
    unsigned int one;
    unsigned int i;
    mbap_stream_t stream;
    capture_t capture;
    one = make_mbap(chunk, 1, 1, pdu, sizeof(pdu));
    for (i = 1; i < 10; ++i)
        make_mbap(chunk + i * one, (unsigned short)(i + 1), 1, pdu, sizeof(pdu));
    mbap_stream_init(&stream); memset(&capture, 0, sizeof(capture));
    CHECK(mbap_stream_feed(&stream, chunk, one * 3U, capture_frame, &capture) == MBAP_OK);
    CHECK(capture.count == 3U && mbap_stream_buffered(&stream) == 0U);
    mbap_stream_init(&stream); memset(&capture, 0, sizeof(capture));
    CHECK(mbap_stream_feed(&stream, chunk, one + one / 2U, capture_frame, &capture) == MBAP_OK);
    CHECK(capture.count == 1U && mbap_stream_buffered(&stream) == one / 2U);
    CHECK(mbap_stream_feed(&stream, chunk + one + one / 2U, one - one / 2U,
                           capture_frame, &capture) == MBAP_OK);
    CHECK(capture.count == 2U && mbap_stream_buffered(&stream) == 0U);
    mbap_stream_init(&stream); memset(&capture, 0, sizeof(capture));
    CHECK(mbap_stream_feed(&stream, chunk, one * 10U, capture_frame, &capture) == MBAP_OK);
    CHECK(capture.count == 10U);
    mbap_stream_init(&stream); memset(&capture, 0, sizeof(capture));
    CHECK(mbap_stream_feed(&stream, chunk, one * 3U, capture_one, &capture) == MBAP_STOPPED);
    CHECK(capture.count == 0U && mbap_stream_buffered(&stream) == one * 3U);
    CHECK(mbap_stream_feed(&stream, 0, 0, capture_frame, &capture) == MBAP_OK);
    CHECK(capture.count == 3U && mbap_stream_buffered(&stream) == 0U);
}

static void parser_invalid(void)
{
    unsigned char data[MBAP_HEADER_SIZE] = {0,1,0,1,0,2,1};
    unsigned char too_much[MBAP_RX_CAPACITY + 1U];
    mbap_stream_t stream;
    mbap_stream_init(&stream);
    CHECK(mbap_stream_feed(&stream, data, sizeof(data), 0, 0) == MBAP_MALFORMED);
    CHECK(stream.poisoned && stream.malformed == 1U);
    data[2] = 0; data[3] = 0; data[4] = 0; data[5] = 0;
    mbap_stream_init(&stream);
    CHECK(mbap_stream_feed(&stream, data, sizeof(data), 0, 0) == MBAP_MALFORMED);
    data[5] = 1;
    mbap_stream_init(&stream);
    CHECK(mbap_stream_feed(&stream, data, sizeof(data), 0, 0) == MBAP_MALFORMED);
    data[4] = 1; data[5] = 0xff;
    mbap_stream_init(&stream);
    CHECK(mbap_stream_feed(&stream, data, sizeof(data), 0, 0) == MBAP_MALFORMED);
    memset(too_much, 0, sizeof(too_much));
    mbap_stream_init(&stream);
    CHECK(mbap_stream_feed(&stream, too_much, sizeof(too_much), 0, 0) == MBAP_OVERFLOW);
    CHECK(stream.poisoned && stream.overflows == 1U);
}

static void parser_maximum(void)
{
    unsigned char frame[MBAP_ADU_MAX];
    unsigned char pdu[MBAP_PDU_MAX];
    mbap_stream_t stream; capture_t capture; unsigned int i;
    for (i=0;i<sizeof(pdu);++i) pdu[i]=(unsigned char)i;
    CHECK(make_mbap(frame,65535U,255,pdu,sizeof(pdu))==MBAP_ADU_MAX);
    mbap_stream_init(&stream); memset(&capture,0,sizeof(capture));
    CHECK(mbap_stream_feed(&stream,frame,sizeof(frame),capture_frame,&capture)==MBAP_OK);
    CHECK(capture.count==1U && capture.frames[0].pdu_length==MBAP_PDU_MAX);
}

static core_port_config_t config(void)
{
    core_port_config_t c;
    memset(&c, 0, sizeof(c)); c.revision=1; c.baud=9600; c.data_bits=8;
    c.stop_bits=1; c.endpoint_port=502; return c;
}

typedef struct completion_info { unsigned int count; unsigned int owner; unsigned int epoch; } completion_info_t;
static void completion(void *context, const core_transaction_t *transaction,
                       core_transaction_status_t status,
                       const unsigned char *response, unsigned int response_length)
{
    completion_info_t *info=(completion_info_t *)context;
    (void)status; (void)response; (void)response_length;
    ++info->count; info->owner=transaction->owner_id;
    info->epoch=transaction->transport_metadata[MODBUS_META_CLIENT_EPOCH];
}

static void adapter_tests(void)
{
    port_runtime_t port;
    mock_backend_t backend;
    core_port_config_t c = config();
    modbus_tcp_request_t request;
    unsigned int id;
    unsigned char response[260];
    unsigned char rtu[8] = {17,3,2,0,42,0,0,0};
    unsigned short crc;
    unsigned int output_length;
    core_transaction_t tx;
    mock_backend_init(&backend);
    port_runtime_init(&port, 0, &c, mock_backend_ops(), &backend);
    port.state = PORT_READY;
    memset(&request, 0, sizeof(request)); request.client_id=7; request.client_epoch=9;
    request.adu.transaction_id=0xabcdU; request.adu.unit_id=17;
    request.adu.pdu_length=5; request.adu.pdu[0]=3;
    request.adu.pdu[3]=0; request.adu.pdu[4]=1;
    CHECK(modbus_tcp_submit(&port, &request, 1, 10, 20, &id) == CORE_ACCEPTED);
    CHECK(port.queue.count == 1U && port.queue.entries[0].payload[0] == 17);
    CHECK(port.queue.entries[0].owner_id == 7U);
    CHECK(port.queue.entries[0].transport_metadata[0] == 0xabcdU);
    CHECK(port.queue.entries[0].transport_metadata[MODBUS_META_RESERVED] == 0U);
    CHECK(port.queue.entries[0].operation.direction == CORE_OPERATION_TX_THEN_RX);
    CHECK(port.queue.entries[0].operation.response_policy == CORE_RESPONSE_BACKEND_FRAMED);
    tx = port.queue.entries[0];
    crc = modbus_crc16(rtu, 5); rtu[5]=(unsigned char)crc; rtu[6]=(unsigned char)(crc>>8);
    CHECK(modbus_tcp_build_response(&tx, rtu, 7, response, sizeof(response), &output_length) == 0);
    CHECK(output_length == 11U && response[0] == 0xab && response[1] == 0xcd);
    CHECK(response[4] == 0 && response[5] == 5 && response[6] == 17 && response[7] == 3);
    rtu[1] = 0x83; rtu[2] = 2; crc=modbus_crc16(rtu,3); rtu[3]=(unsigned char)crc; rtu[4]=(unsigned char)(crc>>8);
    CHECK(modbus_tcp_build_response(&tx, rtu, 5, response, sizeof(response), &output_length) == 0);
    CHECK(output_length == 9U && response[7] == 0x83 && response[8] == 2);
    rtu[4] ^= 1;
    CHECK(modbus_tcp_build_response(&tx, rtu, 5, response, sizeof(response), &output_length) == -2);
    rtu[4] ^= 1; rtu[0] = 18; crc=modbus_crc16(rtu,3);
    rtu[3]=(unsigned char)crc; rtu[4]=(unsigned char)(crc>>8);
    CHECK(modbus_tcp_build_response(&tx, rtu, 5, response, sizeof(response), &output_length) == -3);
    rtu[0] = 17; rtu[1] = 4; crc=modbus_crc16(rtu,3);
    rtu[3]=(unsigned char)crc; rtu[4]=(unsigned char)(crc>>8);
    CHECK(modbus_tcp_build_response(&tx, rtu, 5, response, sizeof(response), &output_length) == -4);
    CHECK(modbus_crc16((const unsigned char *)"123456789", 9) == 0x4b37U);
}

static void dispatcher_tests(void)
{
    modbus_dispatcher_t d;
    modbus_tcp_request_t a, b;
    port_runtime_t port;
    mock_backend_t backend;
    core_port_config_t c=config();
    modbus_dispatcher_init(&d); mock_backend_init(&backend);
    port_runtime_init(&port,0,&c,mock_backend_ops(),&backend); port.state=PORT_READY;
    CHECK(modbus_dispatcher_connect(&d, 10, 1) == 0);
    CHECK(modbus_dispatcher_connect(&d, 20, 1) == 0);
    memset(&a,0,sizeof(a)); a.client_id=10;a.client_epoch=1;a.adu.unit_id=1;a.adu.pdu_length=1;a.adu.pdu[0]=3;
    b=a;b.client_id=20;
    CHECK(modbus_dispatcher_enqueue(&d,&a)==0); CHECK(modbus_dispatcher_enqueue(&d,&b)==0);
    CHECK(modbus_dispatcher_step(&d,&port,1,10,10)==CORE_ACCEPTED);
    CHECK(modbus_dispatcher_step(&d,&port,1,10,10)==CORE_ACCEPTED);
    CHECK(port.queue.entries[0].owner_id != port.queue.entries[1].owner_id);
    CHECK(modbus_dispatcher_enqueue(&d,&a)==0); CHECK(modbus_dispatcher_enqueue(&d,&a)==0);
    CHECK(modbus_dispatcher_enqueue(&d,&a)==-2);
    modbus_dispatcher_disconnect(&d,10,1);
    CHECK(!modbus_dispatcher_client_current(&d,10,1));
    CHECK(d.disconnected_drops==2U);
    CHECK(modbus_dispatcher_connect(&d,10,2)==0);
    CHECK(!modbus_dispatcher_client_current(&d,10,1));
    CHECK(modbus_dispatcher_client_current(&d,10,2));
    port.state=PORT_DISABLED;
    CHECK(modbus_dispatcher_enqueue(&d,&b)==0);
    CHECK(modbus_dispatcher_step(&d,&port,1,10,10)==CORE_PORT_DISABLED);
    port.state=PORT_RECOVERING;
    CHECK(modbus_dispatcher_step(&d,&port,1,10,10)==CORE_PORT_RECOVERING);
    port.state=PORT_RECONFIGURING;
    CHECK(modbus_dispatcher_step(&d,&port,1,10,10)==CORE_RECONFIGURING);
}

static void disconnect_active_test(void)
{
    modbus_dispatcher_t d; modbus_tcp_request_t r; port_runtime_t port;
    mock_backend_t backend; core_port_config_t c=config(); completion_info_t info;
    memset(&info,0,sizeof(info)); modbus_dispatcher_init(&d); mock_backend_init(&backend);
    port_runtime_init(&port,0,&c,mock_backend_ops(),&backend); port.state=PORT_READY;
    port_runtime_set_completion(&port,completion,&info);
    CHECK(modbus_dispatcher_connect(&d,30,1)==0);
    memset(&r,0,sizeof(r));r.client_id=30;r.client_epoch=1;r.adu.unit_id=1;
    r.adu.pdu_length=1;r.adu.pdu[0]=3;
    CHECK(modbus_dispatcher_enqueue(&d,&r)==0);
    CHECK(modbus_dispatcher_step(&d,&port,1,10,10)==CORE_ACCEPTED);
    CHECK(mock_backend_add_plan(&backend,MOCK_DELAYED_SUCCESS,2)==0);
    port_runtime_step(&port,1);port_runtime_step(&port,1);
    CHECK(port.state==PORT_WAITING);
    modbus_dispatcher_disconnect(&d,30,1);
    port_runtime_step(&port,3);
    CHECK(port.state==PORT_READY && info.count==1U && info.owner==30U && info.epoch==1U);
    CHECK(!modbus_dispatcher_client_current(&d,30,1));
    CHECK(modbus_dispatcher_connect(&d,30,2)==0);
    r.client_epoch=2; CHECK(modbus_dispatcher_enqueue(&d,&r)==0);
    CHECK(modbus_dispatcher_step(&d,&port,4,10,10)==CORE_ACCEPTED);
}

static void soak(void)
{
    unsigned char frame[32]; unsigned char pdu[5]={3,0,1,0,1};
    unsigned int length=make_mbap(frame,0,1,pdu,sizeof(pdu));
    unsigned int i, split; mbap_stream_t s; capture_t c;
    mbap_stream_init(&s); memset(&c,0,sizeof(c));
    for (i=0;i<100000U;++i) {
        frame[0]=(unsigned char)(i>>8);frame[1]=(unsigned char)i;
        split=(i%length)+1U;
        CHECK(mbap_stream_feed(&s,frame,split,capture_frame,&c)==MBAP_OK);
        if (split<length) CHECK(mbap_stream_feed(&s,frame+split,length-split,capture_frame,&c)==MBAP_OK);
        if (c.count==16U) c.count=0;
        CHECK(mbap_stream_buffered(&s)==0U);
    }
    CHECK(s.frames==100000U && !s.poisoned);
}

static void framing_tests(void)
{
    unsigned char request[8]={8,4,0,0,0,40,0,0};
    unsigned char normal[85]; unsigned char exception[5];
    unsigned int length=0,i; unsigned short crc;
    memset(normal,0,sizeof(normal)); normal[0]=8;normal[1]=4;normal[2]=80;
    for(i=0;i<80;++i)normal[3+i]=(unsigned char)i;
    crc=modbus_crc16(normal,83);normal[83]=(unsigned char)crc;normal[84]=(unsigned char)(crc>>8);
    exception[0]=8;exception[1]=0x84;exception[2]=2;
    crc=modbus_crc16(exception,3);exception[3]=(unsigned char)crc;exception[4]=(unsigned char)(crc>>8);
    CHECK(modbus_rtu_frame_probe(0,request,8,normal,84,&length)==UART_FRAME_INCOMPLETE);
    CHECK(modbus_rtu_frame_probe(0,request,8,normal,85,&length)==UART_FRAME_COMPLETE&&length==85);
    CHECK(modbus_rtu_frame_probe(0,request,8,exception,4,&length)==UART_FRAME_INCOMPLETE);
    CHECK(modbus_rtu_frame_probe(0,request,8,exception,5,&length)==UART_FRAME_COMPLETE&&length==5);
    exception[4]^=1;
    CHECK(modbus_rtu_frame_probe(0,request,8,exception,5,&length)==UART_FRAME_MALFORMED);
    exception[4]^=1;
    normal[2]=252;
    CHECK(modbus_rtu_frame_probe(0,request,8,normal,3,&length)==UART_FRAME_MALFORMED);
}

static void identity_operation_tests(void)
{
    static const unsigned int tids[]={0,1,0x1001,0xffff};
    static const unsigned int units[]={1,8,247};
    unsigned int ti,ui; core_port_config_t c=config();
    for(ti=0;ti<4;++ti)for(ui=0;ui<3;++ui){
        port_runtime_t port;mock_backend_t backend;modbus_tcp_request_t r;
        mock_backend_init(&backend);port_runtime_init(&port,0,&c,mock_backend_ops(),&backend);port.state=PORT_READY;
        memset(&r,0,sizeof(r));r.client_id=2;r.client_epoch=9;r.adu.transaction_id=tids[ti];r.adu.unit_id=(unsigned char)units[ui];
        r.adu.pdu_length=5;r.adu.pdu[0]=4;r.adu.pdu[4]=1;
        CHECK(modbus_tcp_submit(&port,&r,0,10,10,0)==CORE_ACCEPTED);
        CHECK(port.queue.entries[0].operation.direction==CORE_OPERATION_TX_THEN_RX);
        CHECK(port.queue.entries[0].operation.response_policy==CORE_RESPONSE_BACKEND_FRAMED);
        CHECK(port.queue.entries[0].transport_metadata[0]==tids[ti]);
        CHECK((port.queue.entries[0].transport_metadata[1]>>8)==units[ui]);
    }
}

int main(void)
{
    parser_fragmentation(); parser_coalescing(); parser_invalid(); parser_maximum();
    adapter_tests(); framing_tests(); identity_operation_tests(); dispatcher_tests(); disconnect_active_test(); soak();
    printf("modbus_tcp checks=%lu failed=%lu parsed=100000\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
