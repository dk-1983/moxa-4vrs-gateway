#include "web/web_gateway.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){gateway_network_runtime_t n;char addresses[2][16];memset(&n,0,sizeof(n));
 CHECK(web_gateway_network_error(&n,0,addresses)==60);
 n.observed_valid=1;n.observed.lan[0].up=n.observed.lan[0].link=1;strcpy(n.observed.lan[0].address,"10.0.2.13");
 n.observed.lan[1].up=1;strcpy(n.observed.lan[1].address,"192.168.4.127");
 CHECK(web_gateway_network_error(&n,0,addresses)==0&&!strcmp(addresses[0],"10.0.2.13")&&!addresses[1][0]);
 CHECK(web_gateway_network_error(&n,1,addresses)==2);CHECK(web_gateway_network_error(&n,2,addresses)==2);
 n.observation_error=1;CHECK(web_gateway_network_error(&n,0,addresses)==61);n.observation_error=0;
 n.observed.lan[0].link=0;CHECK(web_gateway_network_error(&n,0,addresses)==2);n.observed.lan[0].link=1;
 n.observed.lan[0].up=0;CHECK(web_gateway_network_error(&n,0,addresses)==2);n.observed.lan[0].up=1;
 n.observed.lan[0].address[0]=0;CHECK(web_gateway_network_error(&n,0,addresses)==2);
 strcpy(n.observed.lan[0].address,"0.0.0.0");CHECK(web_gateway_network_error(&n,0,addresses)==2);
 CHECK(web_gateway_network_error(&n,3,addresses)==2);
 puts("PASS Web gate: LAN1 with unselected LAN2 down; selected LAN2/both refuse; missing/error snapshots separated; selected LAN1 link/up/address failures refuse; no fallback");return 0;}
