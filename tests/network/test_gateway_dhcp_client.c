#include <stdio.h>
#include <string.h>
#include "network/gateway_dhcp_client.h"
#include "network/gateway_dhcp_wire.h"
#include "dhcp_bench_packets.h"
static unsigned int checks,failed;
#define CHECK(x) do{++checks;if(!(x)){++failed;printf("FAIL %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
static const unsigned char mac[6]={2,0,0,0,0,1};
static void number(unsigned char *p,uint32_t n)
{p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;}
static size_t reply(unsigned char *p,uint32_t xid,unsigned int type,uint32_t seconds)
{
    static const unsigned char options[]={53,1,0,54,4,192,0,2,1,1,4,255,255,255,0,
        3,4,192,0,2,1,6,8,192,0,2,1,192,0,2,2,51,4,0,0,0,0,255};
    memset(p,0,512);p[0]=2;p[1]=1;p[2]=6;number(p+4,xid);number(p+16,0xc0000264UL);
    memcpy(p+28,mac,6);number(p+236,0x63825363UL);memcpy(p+240,options,sizeof(options));
    p[242]=(unsigned char)type;number(p+273,seconds);return 240U+sizeof(options);
}
static void bind(gateway_dhcp_client_t *d,core_tick_t now,uint32_t seconds)
{
    unsigned char p[512];size_t n;
    gateway_dhcp_start(d,mac,0x12345678UL,now);gateway_dhcp_step(d,now,1);
    CHECK(gateway_dhcp_packet(d,p,sizeof(p),now)==300U);CHECK(p[242]==1U);
    n=reply(p,d->xid,2,seconds);CHECK(gateway_dhcp_receive(d,p,n,now)==0);
    CHECK(!d->valid&&d->state==GATEWAY_DHCP_REQUESTING);
    CHECK(gateway_dhcp_packet(d,p,sizeof(p),now)==300U);CHECK(p[242]==3U);
    n=reply(p,d->xid,5,seconds);CHECK(gateway_dhcp_receive(d,p,n,now)==1);
    CHECK(!d->valid&&d->state==GATEWAY_DHCP_PROBING);
    CHECK(gateway_dhcp_probed(d,now+100U)==0);CHECK(d->valid);
}
static void wire(void)
{
    gateway_dhcp_client_t d;unsigned char frame[1024],packet[512],saved[1024];size_t n,length,i;
    gateway_dhcp_start(&d,mac,88,0);gateway_dhcp_step(&d,0,1);
    n=gateway_dhcp_broadcast(&d,frame,sizeof(frame),0);CHECK(n==342U);
    CHECK(frame[34]==0&&frame[35]==68&&frame[36]==0&&frame[37]==67);
    /* Server reply with a valid IP checksum and permitted zero UDP checksum. */
    length=reply(packet,88,2,60);memcpy(frame+42,packet,length);
    memcpy(frame,mac,6);frame[11]=2;frame[35]=67;frame[37]=68;
    memcpy(saved,frame,n);
    for(i=0;i<42U;++i)CHECK(gateway_dhcp_frame(&d,frame,i,0)<0);
    frame[24]^=1;CHECK(gateway_dhcp_frame(&d,frame,n,0)<0);memcpy(frame,saved,n);
    frame[40]=1;CHECK(gateway_dhcp_frame(&d,frame,n,0)<0);memcpy(frame,saved,n);
    frame[20]=0x20;CHECK(gateway_dhcp_frame(&d,frame,n,0)<0);memcpy(frame,saved,n);
    CHECK(gateway_dhcp_frame(&d,frame,n,0)==0&&d.state==GATEWAY_DHCP_REQUESTING);
    n=gateway_dhcp_arp(&d,frame,sizeof(frame),0);CHECK(n==42U);
    CHECK(!frame[28]&&!frame[29]&&!frame[30]&&!frame[31]);CHECK(frame[38]==192&&frame[41]==100);
    CHECK(!gateway_dhcp_arp_conflict(&d,frame,n));frame[27]=2;
    CHECK(gateway_dhcp_arp_conflict(&d,frame,n));frame[41]=101;
    CHECK(!gateway_dhcp_arp_conflict(&d,frame,n));
    CHECK(gateway_dhcp_arp(&d,frame,sizeof(frame),1)==42U);frame[27]=2;
    CHECK(gateway_dhcp_arp_conflict(&d,frame,42));
}
static void recorded_bench(void)
{
    gateway_dhcp_client_t d;unsigned char frame[600],packet[600];core_tick_t renew,expiry;size_t n;
    gateway_dhcp_start(&d,bench_payload_0+28,0x02f4beaaUL,0);
    CHECK(gateway_dhcp_receive(&d,bench_payload_0,sizeof(bench_payload_0),0)==0);
    CHECK(gateway_dhcp_packet(&d,packet,sizeof(packet),0)==300U);
    CHECK(gateway_dhcp_receive(&d,bench_payload_1,sizeof(bench_payload_1),1)==1);
    CHECK(d.renew_at==30000U&&d.rebind_at==52000U&&d.expires_at==60000U);
    CHECK(gateway_dhcp_probed(&d,4500)==0);
    gateway_dhcp_step(&d,30000,1);CHECK(d.state==GATEWAY_DHCP_RENEWING&&d.xid==0x02f4beabUL);
    CHECK(gateway_dhcp_packet(&d,packet,sizeof(packet),30000)==300U);
    CHECK(gateway_dhcp_receive(&d,bench_payload_2,sizeof(bench_payload_2),30001)==1);
    CHECK(d.valid&&d.state==GATEWAY_DHCP_BOUND&&d.expires_at==90000U);
    renew=d.renew_at;gateway_dhcp_step(&d,renew,1);
    CHECK(gateway_dhcp_packet(&d,packet,sizeof(packet),renew)==300U);
    n=bench_reply(&d,3,frame);expiry=d.expires_at;
    frame[40]^=1;CHECK(gateway_dhcp_frame(&d,frame,n,renew+1U)<0&&d.expires_at==expiry);frame[40]^=1;
    CHECK(gateway_dhcp_frame(&d,frame,n,renew+9000U)==1);
    CHECK(d.valid&&d.expires_at==renew+60000U); /* Not ACK time + lease. */
    gateway_dhcp_stop(&d);CHECK(gateway_dhcp_frame(&d,frame,n,renew+9010U)<0&&!d.valid);
}
int main(void)
{
    recorded_bench();
    gateway_dhcp_client_t d,before;unsigned char p[512],q[512];size_t n,i;core_tick_t start=0xfffff000U;
    wire();bind(&d,start,60);CHECK(!strcmp(d.lease.address,"192.0.2.100"));CHECK(!strcmp(d.lease.dns[1],"192.0.2.2"));
    gateway_dhcp_step(&d,start+29999U,1);CHECK(d.state==GATEWAY_DHCP_BOUND&&!d.send_type);
    gateway_dhcp_step(&d,start+30000U,1);CHECK(d.state==GATEWAY_DHCP_RENEWING&&d.valid);
    CHECK(gateway_dhcp_packet(&d,p,sizeof(p),start+30000U)==300U);
    CHECK(p[12]==192U&&p[15]==100U&&!p[10]);
    n=reply(p,d.xid,5,60);before=d;
    for(i=0;i<n;++i){d=before;CHECK(gateway_dhcp_receive(&d,p,i,start+31000U)<0);CHECK(!memcmp(&d,&before,sizeof(d)));}
    d=before;memcpy(q,p,n);q[28]^=1;CHECK(gateway_dhcp_receive(&d,q,n,start+31000U)<0);
    memcpy(q,p,n);q[7]^=1;CHECK(gateway_dhcp_receive(&d,q,n,start+31000U)<0);
    memcpy(q,p,n);q[248]=2;CHECK(gateway_dhcp_receive(&d,q,n,start+31000U)<0);
    CHECK(gateway_dhcp_receive(&d,p,n,start+31000U)==1&&d.valid&&d.state==GATEWAY_DHCP_BOUND);
    gateway_dhcp_step(&d,start+84000U,1);CHECK(d.state==GATEWAY_DHCP_REBINDING&&d.valid);
    CHECK(gateway_dhcp_packet(&d,q,sizeof(q),start+84000U)==300U);
    n=reply(p,d.xid,5,60);CHECK(gateway_dhcp_receive(&d,p,n,start+91000U)<0);CHECK(!d.valid);
    CHECK(d.state==GATEWAY_DHCP_SELECTING&&d.changed);
    bind(&d,1000,4);gateway_dhcp_step(&d,5000,1);CHECK(!d.valid&&d.state==GATEWAY_DHCP_SELECTING);
    bind(&d,1000,60);gateway_dhcp_step(&d,2000,0);CHECK(!d.valid&&!d.lease.address[0]&&!d.send_type);
    gateway_dhcp_stop(&d);before=d;n=reply(p,d.xid-1U,5,60);
    CHECK(gateway_dhcp_receive(&d,p,n,2100)<0&&!memcmp(&d,&before,sizeof(d)));
    gateway_dhcp_step(&d,100000,1);CHECK(d.state==GATEWAY_DHCP_OFF);
    gateway_dhcp_start(&d,mac,77,0);n=reply(p,77,2,60);CHECK(gateway_dhcp_receive(&d,p,n,0)==0);
    n=reply(p,77,5,60);p[19]=101;before=d;CHECK(gateway_dhcp_receive(&d,p,n,1)<0&&!memcmp(&d,&before,sizeof(d)));
    n=reply(p,77,6,0);CHECK(gateway_dhcp_receive(&d,p,n,1)==0&&!d.valid&&d.state==GATEWAY_DHCP_BACKOFF);
    gateway_dhcp_start(&d,mac,78,0);n=reply(p,78,2,60);CHECK(gateway_dhcp_receive(&d,p,n,0)==0);
    n=reply(p,78,5,60);CHECK(gateway_dhcp_receive(&d,p,n,0)==1);gateway_dhcp_conflict(&d,500);
    CHECK(!d.valid&&d.state==GATEWAY_DHCP_BACKOFF);
    CHECK(gateway_dhcp_packet(&d,p,sizeof(p),500)==300U&&p[242]==4U);
    gateway_dhcp_step(&d,10499,1);CHECK(d.state==GATEWAY_DHCP_BACKOFF);
    gateway_dhcp_step(&d,10500,1);CHECK(d.state==GATEWAY_DHCP_SELECTING);
    printf("DHCP payload lifecycle: %u checks, %u failed\n",checks,failed);return failed?1:0;
}
