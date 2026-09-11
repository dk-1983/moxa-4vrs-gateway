#ifndef FOURVRS_DHCP_WIRE_H
#define FOURVRS_DHCP_WIRE_H
#include "network/gateway_dhcp_client.h"
/* Ethernet/IPv4/UDP parsing accepts one complete unfragmented datagram.
 * No packed structs, alignment assumptions or native byte order on the wire. */
int gateway_dhcp_frame(gateway_dhcp_client_t *,const unsigned char *,size_t,core_tick_t);
size_t gateway_dhcp_broadcast(gateway_dhcp_client_t *,unsigned char *,size_t,core_tick_t);
size_t gateway_dhcp_arp(const gateway_dhcp_client_t *,unsigned char *,size_t,unsigned int announce);
int gateway_dhcp_arp_conflict(const gateway_dhcp_client_t *,const unsigned char *,size_t);
#endif
