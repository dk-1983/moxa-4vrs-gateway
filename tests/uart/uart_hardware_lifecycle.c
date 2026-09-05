#include <stdio.h>
#include <string.h>

#include "uart/uart_backend.h"

static core_port_config_t safe_config(void)
{
    core_port_config_t c;
    memset(&c, 0, sizeof(c)); c.revision=1; c.mode=0; c.baud=9600;
    c.data_bits=8; c.parity=0; c.stop_bits=1; c.endpoint_port=502;
    return c;
}

int main(void)
{
    uart_backend_t uart; core_port_config_t c=safe_config(); unsigned int i;
    if (uart_backend_init(&uart,0,&c,uart_posix_syscalls(),0)!=0) return 2;
    for(i=0;i<100;++i) {
        if (uart_backend_ops()->open(&uart,i)!=0) { printf("open_failed cycle=%u\n",i); return 3; }
        if (uart_backend_ops()->stop(&uart,i)!=0) { printf("close_failed cycle=%u\n",i); return 4; }
    }
    printf("uart=/dev/ttyM0 cycles=100 opens=%u closes=%u bytes_tx=%u bytes_rx=%u status=OK\n",
           uart.stats.opens,uart.stats.closes,uart.stats.bytes_written,uart.stats.bytes_read);
    return 0;
}
