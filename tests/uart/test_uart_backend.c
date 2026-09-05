#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "core/mock_backend.h"
#include "uart/uart_backend.h"

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL line=%d expr=%s\n", __LINE__, #x); } } while (0)

typedef struct fake_uart {
    int error;
    int open_failures;
    int config_failures;
    int close_failures;
    int fd_open;
    int next_fd;
    unsigned int mode;
    unsigned int special;
    unsigned int write_limit;
    unsigned int read_limit;
    int write_eagain;
    int write_eintr;
    int read_eagain;
    int read_eintr;
    int hard_read;
    unsigned char input[1024];
    unsigned int input_length;
    unsigned int input_offset;
    unsigned char output[1024];
    unsigned int output_length;
    unsigned int opens;
    unsigned int closes;
    unsigned int flushes;
} fake_uart_t;

static int f_open(void *c, const char *path, int flags)
{
    fake_uart_t *f = c; (void)flags;
    CHECK(strcmp(path, "/dev/ttyM0") == 0);
    if (f->open_failures-- > 0) { f->error = EIO; return -1; }
    f->fd_open = 1; ++f->opens; return f->next_fd;
}
static int f_close(void *c, int fd)
{ fake_uart_t *f = c; (void)fd; ++f->closes; if (f->close_failures-- > 0) return -1; f->fd_open = 0; return 0; }
static ssize_t f_read(void *c, int fd, void *buffer, size_t length)
{
    fake_uart_t *f = c; unsigned int remain; unsigned int count; (void)fd;
    if (f->read_eintr-- > 0) { f->error = EINTR; return -1; }
    if (f->read_eagain-- > 0) { f->error = EAGAIN; return -1; }
    if (f->hard_read) { f->error = EIO; return -1; }
    remain = f->input_length - f->input_offset;
    if (remain == 0) { f->error = EAGAIN; return -1; }
    count = remain < length ? remain : (unsigned int)length;
    if (f->read_limit && count > f->read_limit) count = f->read_limit;
    memcpy(buffer, f->input + f->input_offset, count); f->input_offset += count;
    return (ssize_t)count;
}
static ssize_t f_write(void *c, int fd, const void *buffer, size_t length)
{
    fake_uart_t *f = c; unsigned int count = (unsigned int)length; (void)fd;
    if (f->write_eintr-- > 0) { f->error = EINTR; return -1; }
    if (f->write_eagain-- > 0) { f->error = EAGAIN; return -1; }
    if (f->write_limit && count > f->write_limit) count = f->write_limit;
    memcpy(f->output + f->output_length, buffer, count); f->output_length += count;
    return (ssize_t)count;
}
static int f_ioctl(void *c, int fd, unsigned long request, void *argument)
{
    fake_uart_t *f = c; unsigned int *v = argument; (void)fd;
    if (f->config_failures-- > 0) return -1;
    if (request == FOURVRS_MOXA_SET_OP_MODE) f->mode = *v;
    else if (request == FOURVRS_MOXA_GET_OP_MODE) *v = f->mode;
    else if (request == FOURVRS_MOXA_SET_SPECIAL_BAUD_RATE) f->special = *v;
    else if (request == FOURVRS_MOXA_GET_SPECIAL_BAUD_RATE) *v = f->special;
    else return -1;
    return 0;
}
static int f_tcget(void *c, int fd, struct termios *v) { (void)c; (void)fd; memset(v, 0, sizeof(*v)); return 0; }
static int f_tcset(void *c, int fd, int action, const struct termios *v) { fake_uart_t *f=c; (void)fd;(void)action;(void)v; return f->config_failures-- > 0 ? -1 : 0; }
static int f_flush(void *c, int fd, int selector) { fake_uart_t *f=c;(void)fd;(void)selector;++f->flushes;f->input_offset=f->input_length;return 0; }
static int f_error(void *c) { return ((fake_uart_t *)c)->error; }

static const uart_syscalls_t fake_calls = {
    f_open, f_close, f_read, f_write, f_ioctl, f_tcget, f_tcset, f_flush, f_error
};

typedef struct trace_recorder {
    unsigned int calls;
    unsigned int positive;
    unsigned int recovery;
    unsigned int first_before;
    unsigned int last_after;
    unsigned char bytes[UART_RX_CAPACITY];
    unsigned int byte_count;
} trace_recorder_t;

static void record_read(void *context,const uart_read_trace_event_t *event)
{
    trace_recorder_t *record=(trace_recorder_t *)context;
    ++record->calls;
    if(event->phase==UART_READ_RECOVERY)++record->recovery;
    if(event->result>0){if(record->positive==0U)record->first_before=event->rx_length_before;++record->positive;record->last_after=event->rx_length_after;if(record->byte_count+event->byte_count<=UART_RX_CAPACITY){memcpy(record->bytes+record->byte_count,event->bytes,event->byte_count);record->byte_count+=event->byte_count;}}
}

static core_port_config_t config(unsigned int revision)
{
    core_port_config_t c;
    memset(&c, 0, sizeof(c)); c.revision=revision; c.mode=0; c.baud=9600;
    c.data_bits=8; c.parity=0; c.stop_bits=1; c.endpoint_port=502;
    return c;
}

static void fake_init(fake_uart_t *f)
{ memset(f, 0, sizeof(*f)); f->next_fd=7; f->write_limit=64; }

static void transaction_init(core_transaction_t *tx, unsigned int generation,
                             unsigned int response_length)
{
    memset(tx, 0, sizeof(*tx)); tx->generation=generation; tx->payload_length=100;
    tx->operation.direction=CORE_OPERATION_TX_THEN_RX;
    tx->operation.response_policy=CORE_RESPONSE_FIXED_LENGTH;
    tx->operation.expected_response_length=response_length;
    memset(tx->payload, 0x5a, tx->payload_length);
}

static void test_metadata_is_opaque(void)
{
    fake_uart_t f; uart_backend_t uart; core_port_config_t c=config(1);
    core_transaction_t a, b;
    fake_init(&f); CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0);
    CHECK(uart_backend_ops()->open(&uart,0)==0);
    transaction_init(&a,50,3); b=a; b.generation=51;
    a.transport_metadata[0]=0; a.transport_metadata[1]=0x0804;
    a.transport_metadata[2]=1; a.transport_metadata[3]=0xffffffffU;
    b.transport_metadata[0]=0xffffU; b.transport_metadata[1]=0xf704;
    b.transport_metadata[2]=0xffffffffU; b.transport_metadata[3]=0;
    CHECK(uart_backend_ops()->start(&uart,&a,0)==0);
    CHECK(uart.operation.direction==CORE_OPERATION_TX_THEN_RX);
    CHECK(uart.expected_response_length==3);
    uart_backend_ops()->cancel(&uart,50);
    CHECK(uart_backend_ops()->start(&uart,&b,0)==0);
    CHECK(uart.operation.direction==CORE_OPERATION_TX_THEN_RX);
    CHECK(uart.expected_response_length==3);
}

static void test_config_and_lifecycle(void)
{
    fake_uart_t f; uart_backend_t uart; core_port_config_t c=config(1); unsigned int i;
    fake_init(&f); CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0);
    CHECK(strcmp(uart.device_path,"/dev/ttyM0")==0);
    CHECK(uart_backend_ops()->open(&uart,0)==0); CHECK(f.mode==0); CHECK(uart.configured);
    for(i=0;i<1000;++i){ CHECK(uart_backend_ops()->stop(&uart,i)==0); CHECK(uart_backend_ops()->open(&uart,i)==0); }
    CHECK(f.opens==1001); CHECK(f.closes==1000); CHECK(f.fd_open);
    CHECK(uart_backend_ops()->stop(&uart,1001)==0); CHECK(!f.fd_open);
    c.mode=4; CHECK(uart_config_validate(&c)!=0); c.mode=0; c.baud=12345; CHECK(uart_config_validate(&c)!=0);
}

static void test_configuration_matrix(void)
{
    static const unsigned int rates[] = {
        50,75,110,134,150,200,300,600,1200,1800,2400,4800,9600,
        19200,38400,57600,115200,230400,460800,500000,576000,921600
    };
    fake_uart_t f; uart_backend_t uart; core_port_config_t c=config(1);
    unsigned int i, mode, bits, parity, stop;
    fake_init(&f); CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0);
    CHECK(uart_backend_ops()->open(&uart,0)==0);
    for(i=0;i<sizeof(rates)/sizeof(rates[0]);++i) {
        c.baud=rates[i]; CHECK(uart_config_validate(&c)==0);
        CHECK(uart_backend_ops()->reconfigure(&uart,&c,i)==0);
    }
    for(mode=0;mode<4;++mode) for(bits=5;bits<=8;++bits)
        for(parity=0;parity<3;++parity) for(stop=1;stop<=2;++stop) {
            c.mode=mode; c.data_bits=bits; c.parity=parity; c.stop_bits=stop;
            CHECK(uart_backend_ops()->reconfigure(&uart,&c,0)==0);
            CHECK(f.mode==mode);
        }
#ifdef B4000000
    c.special_baud_enabled=1; c.special_baud=250000; c.baud=9600;
    CHECK(uart_backend_ops()->reconfigure(&uart,&c,0)==0); CHECK(f.special==250000);
#endif
    CHECK(uart_backend_ops()->stop(&uart,0)==0);
}

static void test_partial_io_and_temporary_errors(void)
{
    fake_uart_t f; uart_backend_t uart; core_port_config_t c=config(1);
    core_transaction_t tx; backend_event_t event; unsigned int steps=0;
    fake_init(&f); f.write_limit=7; f.write_eagain=1; f.write_eintr=1;
    memcpy(f.input,"response-data",13); f.input_length=13; f.read_limit=3; f.read_eagain=1; f.read_eintr=1;
    CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0); CHECK(uart_backend_ops()->open(&uart,0)==0);
    transaction_init(&tx,42,13); CHECK(uart_backend_ops()->start(&uart,&tx,0)==0);
    do { CHECK(uart_backend_ops()->poll(&uart,steps++,&event)>=0); CHECK(steps<100); } while(event.type==BACKEND_EVENT_NONE);
    CHECK(event.type==BACKEND_EVENT_TRANSMITTED);CHECK(event.generation==42);
    do { CHECK(uart_backend_ops()->poll(&uart,steps++,&event)>=0); CHECK(steps<100); } while(event.type==BACKEND_EVENT_NONE);
    CHECK(event.type==BACKEND_EVENT_RESPONSE); CHECK(event.generation==42); CHECK(event.length==13);
    CHECK(memcmp(event.data,"response-data",13)==0); CHECK(f.output_length==100);
    CHECK(uart.stats.short_writes>0); CHECK(uart.stats.temporary_errors==4);
}

static void test_timeout_stale_and_recovery(void)
{
    fake_uart_t f; uart_backend_t uart; core_port_config_t c=config(1);
    core_transaction_t tx; backend_event_t event;
    fake_init(&f); CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0); CHECK(uart_backend_ops()->open(&uart,0)==0);
    transaction_init(&tx,7,4); CHECK(uart_backend_ops()->start(&uart,&tx,0)==0);
    uart_backend_ops()->cancel(&uart,7);
    memcpy(f.input,"late",4); f.input_length=4;
    CHECK(uart_backend_ops()->recover(&uart,20)==0); CHECK(uart.stats.recovery_drained==4); CHECK(f.flushes==1);
    CHECK(uart_backend_ops()->poll(&uart,21,&event)==0); CHECK(event.type==BACKEND_EVENT_NONE);
    f.input_length=UART_RECOVERY_DRAIN_LIMIT; f.input_offset=0;
    CHECK(uart_backend_ops()->recover(&uart,30)!=0);
}

static void test_rx_sentinels_and_raw_chunks(void)
{
    static const unsigned char sentinels[]={0xfe,0xff,0xa5,0x5a};
    static const unsigned int limits[]={1,64,0,7};
    unsigned char response[85];unsigned int pattern,i,steps;
    for(i=0;i<sizeof(response);++i)response[i]=(unsigned char)(i+8U);
    for(pattern=0;pattern<sizeof(sentinels);++pattern){
        fake_uart_t f;uart_backend_t uart;core_port_config_t c=config(1);core_transaction_t tx;backend_event_t event;trace_recorder_t record;
        fake_init(&f);memset(&record,0,sizeof(record));memcpy(f.input,response,sizeof(response));f.input_length=sizeof(response);f.read_limit=limits[pattern];
        CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0);memset(uart.rx,sentinels[pattern],sizeof(uart.rx));uart_backend_set_read_trace(&uart,record_read,&record);CHECK(uart_backend_ops()->open(&uart,0)==0);
        transaction_init(&tx,100U+pattern,sizeof(response));CHECK(uart_backend_ops()->start(&uart,&tx,10)==0);steps=0;
        do{CHECK(uart_backend_ops()->poll(&uart,11U+steps++,&event)>=0);CHECK(steps<300U);}while(event.type!=BACKEND_EVENT_TRANSMITTED);
        do{CHECK(uart_backend_ops()->poll(&uart,11U+steps++,&event)>=0);CHECK(steps<300U);}while(event.type==BACKEND_EVENT_NONE);
        CHECK(event.type==BACKEND_EVENT_RESPONSE&&event.length==sizeof(response));CHECK(memcmp(event.data,response,sizeof(response))==0);CHECK(record.first_before==0U&&record.last_after==sizeof(response));CHECK(record.byte_count==sizeof(response)&&memcmp(record.bytes,response,sizeof(response))==0);CHECK(record.calls==uart.stats.read_calls);
    }
    {
        fake_uart_t f;uart_backend_t uart;core_port_config_t c=config(1);core_transaction_t tx;backend_event_t event;trace_recorder_t record;
        fake_init(&f);memset(&record,0,sizeof(record));CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0);uart_backend_set_read_trace(&uart,record_read,&record);CHECK(uart_backend_ops()->open(&uart,0)==0);transaction_init(&tx,200,85);CHECK(uart_backend_ops()->start(&uart,&tx,0)==0);uart_backend_ops()->cancel(&uart,200);memset(f.input,0xfe,9);f.input_length=9;CHECK(uart_backend_ops()->recover(&uart,20)==0);CHECK(record.recovery>=1U);
        f.input_offset=0;f.input_length=sizeof(response);memcpy(f.input,response,sizeof(response));memset(uart.rx,0xff,sizeof(uart.rx));transaction_init(&tx,201,85);CHECK(uart_backend_ops()->start(&uart,&tx,21)==0);steps=0;do{CHECK(uart_backend_ops()->poll(&uart,22U+steps++,&event)>=0);}while(event.type!=BACKEND_EVENT_TRANSMITTED);do{CHECK(uart_backend_ops()->poll(&uart,22U+steps++,&event)>=0);}while(event.type==BACKEND_EVENT_NONE);CHECK(event.type==BACKEND_EVENT_RESPONSE&&memcmp(event.data,response,sizeof(response))==0);CHECK(uart.recovery_since_previous_transaction==1U);
    }
}

static void test_failures_and_rollback(void)
{
    fake_uart_t f; uart_backend_t uart; core_port_config_t c=config(1), changed=config(2);
    core_transaction_t tx; backend_event_t event;
    fake_init(&f); f.open_failures=1; CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&f)==0);
    CHECK(uart_backend_ops()->open(&uart,0)!=0); CHECK(uart_backend_ops()->open(&uart,1)==0);
    changed.mode=2; f.config_failures=1; CHECK(uart_backend_ops()->reconfigure(&uart,&changed,2)!=0);
    CHECK(uart_backend_ops()->reconfigure(&uart,&c,3)==0); CHECK(uart.config.revision==1);
    transaction_init(&tx,9,2); CHECK(uart_backend_ops()->start(&uart,&tx,0)==0);
    while(uart.tx_offset<uart.tx_length) CHECK(uart_backend_ops()->poll(&uart,0,&event)==0);
    CHECK(uart_backend_ops()->poll(&uart,0,&event)==1);CHECK(event.type==BACKEND_EVENT_TRANSMITTED);
    f.hard_read=1; CHECK(uart_backend_ops()->poll(&uart,0,&event)==1); CHECK(event.type==BACKEND_EVENT_FAILURE);
}

static void test_one_uart_failure_seven_progress(void)
{
    port_runtime_t ports[8]; mock_backend_t mocks[7]; uart_backend_t uart;
    fake_uart_t failed; core_port_config_t c=config(1); unsigned int i;
    fake_init(&failed); failed.open_failures=100;
    CHECK(uart_backend_init(&uart,0,&c,&fake_calls,&failed)==0);
    port_runtime_init(&ports[0],0,&c,uart_backend_ops(),&uart);
    CHECK(port_runtime_start(&ports[0],0,5)==0);
    for(i=1;i<8;++i){ mock_backend_init(&mocks[i-1]); port_runtime_init(&ports[i],i,&c,mock_backend_ops(),&mocks[i-1]); CHECK(port_runtime_start(&ports[i],0,5)==0); }
    for(i=0;i<10;++i){ unsigned int p; for(p=0;p<8;++p) port_runtime_step(&ports[p],i); }
    CHECK(ports[0].state==PORT_RECOVERING);
    for(i=1;i<8;++i) CHECK(ports[i].state==PORT_READY);
}

int main(void)
{
    test_config_and_lifecycle(); test_configuration_matrix();
    test_partial_io_and_temporary_errors();
    test_timeout_stale_and_recovery(); test_failures_and_rollback();
    test_rx_sentinels_and_raw_chunks();
    test_metadata_is_opaque();
    test_one_uart_failure_seven_progress();
    printf("uart_backend_tests checks=%u failed=%u backend_bytes=%u\n",checks,failures,(unsigned int)sizeof(uart_backend_t));
    return failures ? 1 : 0;
}
