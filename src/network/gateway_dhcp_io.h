#ifndef FOURVRS_DHCP_IO_H
#define FOURVRS_DHCP_IO_H
#include "network/gateway_dhcp_wire.h"
typedef struct gateway_dhcp_io { int packet,udp,index; } gateway_dhcp_io_t;
/* Called only by the independent network owner; no executable or hook. */
int gateway_dhcp_io_open(gateway_dhcp_io_t *,unsigned int lan);
void gateway_dhcp_io_close(gateway_dhcp_io_t *);
int gateway_dhcp_io_receive(gateway_dhcp_io_t *,unsigned char *,size_t);
int gateway_dhcp_io_send(gateway_dhcp_io_t *,gateway_dhcp_client_t *,core_tick_t);
int gateway_dhcp_io_probe(gateway_dhcp_io_t *,const gateway_dhcp_client_t *,unsigned int announce);
/* Refuse ownership when any existing client or ambiguous process is present.
 * Does not signal, execute or modify another process. */
int gateway_dhcp_foreign_client(void);
#endif
