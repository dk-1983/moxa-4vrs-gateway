#include <string.h>
#include "network/gateway_dhcp_wire.h"
static unsigned int word(const unsigned char *p){return ((unsigned int)p[0]<<8)|p[1];}
static void put(unsigned char *p,unsigned int n){p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n;}
static unsigned long sum(const unsigned char *p,size_t n,unsigned long value)
{while(n>=2U){value+=word(p);p+=2;n-=2;}if(n)value+=(unsigned long)*p<<8;return value;}
static unsigned int checksum(unsigned long value)
{while(value>>16)value=(value&65535UL)+(value>>16);return (unsigned int)(~value)&65535U;}
static void ip(unsigned char *p,unsigned long n)
{p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;}
int gateway_dhcp_frame(gateway_dhcp_client_t *d,const unsigned char *p,size_t n,core_tick_t now)
{
    const unsigned char *v,*udp;unsigned int header,total,length;unsigned long pseudo;
    static const unsigned char broadcast[6]={255,255,255,255,255,255};
    if(!d||!p||n<42U||n>1514U||word(p+12)!=0x0800U||
       (memcmp(p,d->mac,6)&&memcmp(p,broadcast,6))||!memcmp(p+6,d->mac,6))return -1;
    v=p+14;header=(v[0]&15U)*4U;total=word(v+2);
    if((v[0]>>4)!=4U||header<20U||header>60U||total<header+8U||total>n-14U||
       v[9]!=17U||(word(v+6)&0x3fffU)||checksum(sum(v,header,0)))return -1;
    udp=v+header;length=word(udp+4);
    if(word(udp)!=67U||word(udp+2)!=68U||length!=total-header)return -1;
    pseudo=sum(v+12,8,17UL+length);
    if(word(udp+6)&&checksum(sum(udp,length,pseudo)))return -1;
    return gateway_dhcp_receive(d,udp+8,length-8U,now);
}
size_t gateway_dhcp_broadcast(gateway_dhcp_client_t *d,unsigned char *p,size_t cap,core_tick_t now)
{
    size_t n;unsigned long source=0;
    if(!d||!p||cap<342U)return 0;
    n=gateway_dhcp_packet(d,p+42,cap-42U,now);if(!n)return 0;
    memset(p,0,42);memset(p,255,6);memcpy(p+6,d->mac,6);put(p+12,0x0800);
    p[14]=0x45;p[22]=64;p[23]=17;put(p+16,(unsigned int)n+28U);
    if(d->valid)gateway_ipv4_parse(d->lease.address,&source);
    ip(p+26,source);memset(p+30,255,4);put(p+24,checksum(sum(p+14,20,0)));
    put(p+34,68);put(p+36,67);put(p+38,(unsigned int)n+8U);
    /* IPv4 permits UDP checksum zero. Receive still verifies nonzero checksums. */
    return n+42U;
}
size_t gateway_dhcp_arp(const gateway_dhcp_client_t *d,unsigned char *p,size_t cap,unsigned int announce)
{
    unsigned long target;
    if(!d||!p||cap<42U||gateway_ipv4_parse(d->lease.address,&target))return 0;
    memset(p,0,42);memset(p,255,6);memcpy(p+6,d->mac,6);put(p+12,0x0806);
    put(p+14,1);put(p+16,0x0800);p[18]=6;p[19]=4;put(p+20,1);memcpy(p+22,d->mac,6);
    if(announce)ip(p+28,target);
    ip(p+38,target);return 42;
}
int gateway_dhcp_arp_conflict(const gateway_dhcp_client_t *d,const unsigned char *p,size_t n)
{
    unsigned long target;unsigned char bytes[4];static const unsigned char zero[4]={0,0,0,0};
    if(!d||!p||n<42U||n>1514U||word(p+12)!=0x0806||word(p+14)!=1||word(p+16)!=0x0800||
       p[18]!=6||p[19]!=4||(word(p+20)!=1&&word(p+20)!=2)||!memcmp(p+22,d->mac,6)||
       gateway_ipv4_parse(d->lease.address,&target))return 0;
    ip(bytes,target);
    return !memcmp(p+28,bytes,4)||(!memcmp(p+28,zero,4)&&!memcmp(p+38,bytes,4));
}
