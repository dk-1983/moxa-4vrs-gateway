#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "gateway/gateway_real_adapter.h"

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL line=%u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

typedef struct fake_uart {
    int error;
    int open_failures;
    int configure_failures;
    int close_failures;
    int open;
    unsigned int mode;
    unsigned int opens;
    unsigned int closes;
    unsigned int restores;
} fake_uart_t;

typedef struct fake_listener {
    port_runtime_t *runtime;
    int open_failures;
    int close_failures;
    int active;
    unsigned int opens;
    unsigned int closes;
    unsigned int steps;
    unsigned int clients;
    unsigned int endpoint;
    char address[GATEWAY_BIND_ADDRESS_LENGTH];
} fake_listener_t;

typedef struct fixture {
    gateway_controller_t controller;
    gateway_port_config_t config[GATEWAY_PORT_COUNT];
    gateway_port_binding_t bindings[GATEWAY_PORT_COUNT];
    gateway_real_adapter_t adapter;
    fake_uart_t uart;
    fake_listener_t listener;
} fixture_t;

static int f_open(void *context, const char *path, int flags)
{ fake_uart_t *f=(fake_uart_t *)context;(void)flags;CHECK(strcmp(path,"/dev/ttyM0")==0);if(f->open_failures>0){--f->open_failures;f->error=EIO;return-1;}f->open=1;++f->opens;return 9; }
static int f_close(void *context, int fd)
{ fake_uart_t *f=(fake_uart_t *)context;(void)fd;++f->closes;if(f->close_failures>0){--f->close_failures;return-1;}f->open=0;return 0; }
static ssize_t f_read(void *context, int fd, void *buffer, size_t length)
{ fake_uart_t *f=(fake_uart_t *)context;(void)fd;(void)buffer;(void)length;f->error=EAGAIN;return-1; }
static ssize_t f_write(void *context, int fd, const void *buffer, size_t length)
{ (void)context;(void)fd;(void)buffer;return(ssize_t)length; }
static int f_ioctl(void *context, int fd, unsigned long request, void *argument)
{ fake_uart_t *f=(fake_uart_t *)context;unsigned int *value=(unsigned int *)argument;(void)fd;if(request==FOURVRS_MOXA_GET_OP_MODE)*value=f->mode;else if(request==FOURVRS_MOXA_SET_OP_MODE){f->mode=*value;if(*value==0U)++f->restores;}else if(request==FOURVRS_MOXA_GET_SPECIAL_BAUD_RATE)*value=0U;else if(request!=FOURVRS_MOXA_SET_SPECIAL_BAUD_RATE)return-1;return 0; }
static int f_tcget(void *context, int fd, struct termios *value)
{ (void)context;(void)fd;memset(value,0,sizeof(*value));return 0; }
static int f_tcset(void *context, int fd, int action, const struct termios *value)
{ fake_uart_t *f=(fake_uart_t *)context;(void)fd;(void)action;(void)value;if(f->configure_failures>0){--f->configure_failures;return-1;}return 0; }
static int f_flush(void *context, int fd, int selector)
{ (void)context;(void)fd;(void)selector;return 0; }
static int f_error(void *context) { return ((fake_uart_t *)context)->error; }
static const uart_syscalls_t fake_uart_calls={f_open,f_close,f_read,f_write,f_ioctl,f_tcget,f_tcset,f_flush,f_error};

static void l_init(void *context, port_runtime_t *runtime)
{ ((fake_listener_t *)context)->runtime=runtime; }
static int l_open(void *context,const char *address,unsigned short endpoint)
{ fake_listener_t *l=(fake_listener_t *)context;++l->opens;if(l->open_failures>0){--l->open_failures;return-1;}l->active=1;l->endpoint=endpoint;strncpy(l->address,address,sizeof(l->address)-1U);return 0; }
static void l_step(void *context,core_tick_t now)
{ fake_listener_t *l=(fake_listener_t *)context;(void)now;++l->steps; }
static int l_close(void *context)
{ fake_listener_t *l=(fake_listener_t *)context;++l->closes;if(l->close_failures>0){--l->close_failures;return-1;}l->active=0;l->clients=0;return 0; }
static unsigned int l_clients(const void *context) { return ((const fake_listener_t *)context)->clients; }
static unsigned int l_zero(const void *context) { (void)context;return 0; }
static void l_diagnostics(const void*c,gateway_transport_diagnostics_t*d){(void)c;memset(d,0,sizeof(*d));}
static const gateway_listener_driver_t fake_listener_driver={l_init,l_open,l_step,l_close,l_clients,l_zero,l_zero,l_diagnostics};

static void step(fixture_t *fixture,core_tick_t now,unsigned int count)
{ unsigned int i;for(i=0;i<count;++i)gateway_controller_step(&fixture->controller,now+i); }

static void fixture_init(fixture_t *fixture)
{
    unsigned int i;
    memset(fixture,0,sizeof(*fixture));
    gateway_configuration_defaults(fixture->config);
    fixture->config[0].mode=SERIAL_MODE_RS485_2W;
    fixture->config[0].baud=115200UL;
    fixture->config[0].endpoint_port=1502U;
    for(i=1;i<GATEWAY_PORT_COUNT;++i)fixture->config[i].enabled=0;
    memcpy(fixture->config[0].bind_address,"10.0.2.15",10U);
    CHECK(gateway_real_adapter_init(&fixture->adapter,0,&fixture->config[0],&fake_uart_calls,&fixture->uart,&fake_listener_driver,&fixture->listener)==0);
    fixture->bindings[0]=gateway_real_adapter_binding(&fixture->adapter);
    CHECK(gateway_controller_init(&fixture->controller,fixture->config,fixture->bindings)==0);
    gateway_real_adapter_attach(&fixture->adapter,&fixture->controller.ports[0].runtime);
}

static void successful_lifecycle(void)
{
    fixture_t f;gateway_health_t health;unsigned int i;
    fixture_init(&f);gateway_controller_start(&f.controller,0);step(&f,0,3);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_READY);
    CHECK(f.uart.open&&f.uart.mode==SERIAL_MODE_RS485_2W&&f.uart.opens==1U);
    CHECK(f.listener.active&&f.listener.endpoint==1502U&&strcmp(f.listener.address,"10.0.2.15")==0);
    for(i=1;i<GATEWAY_PORT_COUNT;++i){CHECK(f.controller.ports[i].lifecycle==GATEWAY_PORT_DISABLED);CHECK(f.controller.ports[i].runtime.state==PORT_DISABLED);}
    f.listener.clients=2;gateway_controller_health(&f.controller,&health);
    CHECK(health.enabled_ports==1U&&health.ready_ports==1U&&health.active_clients==2U);
    gateway_controller_shutdown(&f.controller,10);step(&f,10,3);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_DISABLED);
    CHECK(!f.listener.active&&!f.uart.open&&f.uart.mode==0U&&f.uart.restores>=1U);
}

static void startup_failures_unwind(void)
{
    fixture_t f;
    fixture_init(&f);f.uart.open_failures=10000;gateway_controller_start(&f.controller,0);step(&f,0,1100);CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_DEGRADED||f.controller.ports[0].lifecycle==GATEWAY_PORT_ERROR);CHECK(!f.listener.active&&!f.uart.open);
    fixture_init(&f);f.uart.configure_failures=10000;gateway_controller_start(&f.controller,0);step(&f,0,1100);CHECK(!f.listener.active&&!f.uart.open&&f.uart.closes>=1U);
    fixture_init(&f);f.listener.open_failures=1;gateway_controller_start(&f.controller,0);step(&f,0,5);CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_ERROR);CHECK(!f.listener.active&&!f.uart.open&&f.uart.mode==0U);
}

static void stop_failure_visibility(void)
{
    fixture_t f;
    fixture_init(&f);gateway_controller_start(&f.controller,0);step(&f,0,3);f.listener.close_failures=1;gateway_controller_shutdown(&f.controller,10);step(&f,10,3);CHECK(f.controller.ports[0].stop_failures==1U);CHECK(f.controller.ports[0].listener_owned==1U);CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_ERROR);gateway_controller_shutdown(&f.controller,20);step(&f,20,2);CHECK(!f.listener.active);
    fixture_init(&f);gateway_controller_start(&f.controller,0);step(&f,0,3);f.uart.close_failures=10000;gateway_controller_shutdown(&f.controller,10);step(&f,10,1100);CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_ERROR);CHECK(f.controller.ports[0].stop_failures==1U);
}

static void disable_enable_and_generation(void)
{
    fixture_t f;gateway_port_config_t config;unsigned int generation;
    fixture_init(&f);gateway_controller_start(&f.controller,0);step(&f,0,3);generation=f.controller.ports[0].config_generation;
    config=f.controller.ports[0].current;config.enabled=0;config.revision=2;
    CHECK(gateway_controller_reconfigure(&f.controller,0,&config,10)==0);step(&f,10,3);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_DISABLED&&!f.controller.ports[0].current.enabled);
    CHECK(f.controller.ports[0].config_generation==generation+1U&&!f.uart.open&&!f.listener.active);
    config.enabled=1;config.revision=3;CHECK(gateway_controller_reconfigure(&f.controller,0,&config,20)==0);step(&f,20,3);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_READY&&f.controller.ports[0].config_generation==generation+2U);
    CHECK(f.uart.opens==2U&&f.listener.opens==2U);
}

static void reconfigure_and_rollback(void)
{
    fixture_t f;gateway_port_config_t config;
    fixture_init(&f);gateway_controller_start(&f.controller,0);step(&f,0,3);
    config=f.controller.ports[0].current;config.endpoint_port=1602U;config.revision=2;
    CHECK(gateway_controller_reconfigure(&f.controller,0,&config,10)==0);step(&f,10,4);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_READY&&f.controller.ports[0].current.endpoint_port==1602U);
    config.endpoint_port=1702U;config.revision=3;f.listener.open_failures=1;
    CHECK(gateway_controller_reconfigure(&f.controller,0,&config,20)==0);step(&f,20,5);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_READY);
    CHECK(f.controller.ports[0].current.endpoint_port==1602U);
}

static void builtin_transport_switches(void)
{
    fixture_t f; gateway_port_config_t c; unsigned int i;
    struct sockaddr_in sa; socklen_t size=sizeof(sa); int blocker;
    fixture_init(&f); strcpy(f.config[0].bind_address,"127.0.0.1");
    blocker=socket(AF_INET,SOCK_DGRAM,0);CHECK(blocker>=0);
    memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_addr.s_addr=inet_addr("127.0.0.1");
    CHECK(bind(blocker,(struct sockaddr*)&sa,sizeof(sa))==0);
    CHECK(getsockname(blocker,(struct sockaddr*)&sa,&size)==0);
    f.config[0].endpoint_port=ntohs(sa.sin_port);
    CHECK(gateway_real_adapter_init(&f.adapter,0,&f.config[0],&fake_uart_calls,&f.uart,gateway_modbus_listener_driver(),0)==0);
    f.bindings[0]=gateway_real_adapter_binding(&f.adapter);
    CHECK(gateway_controller_init(&f.controller,f.config,f.bindings)==0);
    gateway_real_adapter_attach(&f.adapter,&f.controller.ports[0].runtime);
    gateway_controller_start(&f.controller,0);step(&f,0,10);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_READY);
    c=f.controller.ports[0].current;c.transport=TRANSPORT_MODBUS_UDP;++c.revision;
    /* Occupied UDP endpoint must roll back to the previous TCP listener. */
    CHECK(gateway_controller_reconfigure(&f.controller,0,&c,20)==0);step(&f,20,20);
    CHECK(f.controller.ports[0].current.transport==TRANSPORT_MODBUS_TCP);
    CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_READY);
    close(blocker);
    for(i=0;i<5U;++i){
        c=f.controller.ports[0].current;++c.revision;
        c.transport=i==0?TRANSPORT_MODBUS_UDP:i==1?TRANSPORT_RTU_UDP:i==2?TRANSPORT_RAW_TCP:i==3?TRANSPORT_RAW_UDP:TRANSPORT_MODBUS_TCP;
        CHECK(gateway_controller_reconfigure(&f.controller,0,&c,50U+i*30U)==0);step(&f,50U+i*30U,20);
        CHECK(f.controller.ports[0].current.transport==c.transport);
        CHECK(f.controller.ports[0].lifecycle==GATEWAY_PORT_READY);
        if(i<2U){CHECK(f.adapter.listener_context==&f.adapter.udp_listener);CHECK(f.adapter.udp_listener.rtu_mode==i);}
        else if(i<4U){CHECK(f.adapter.listener_context==&f.adapter);CHECK(f.adapter.raw_listener.udp==(i==3U));}
        else CHECK(f.adapter.listener_context==&f.adapter.listener);
    }
    gateway_controller_shutdown(&f.controller,220);step(&f,220,20);
    CHECK(!f.adapter.listener_started&&!f.uart.open);
}

int main(void)
{
    successful_lifecycle();startup_failures_unwind();stop_failure_visibility();disable_enable_and_generation();reconfigure_and_rollback();builtin_transport_switches();
    printf("gateway_real_adapter checks=%u failed=%u\n",checks,failures);
    return failures?1:0;
}
