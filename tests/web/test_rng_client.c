#define _GNU_SOURCE
#include "web/rng_client.h"
#include "web/rng_wire.h"
#include "web/web_protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void begin(int f[2],unsigned char *b,unsigned int now){assert(!socketpair(AF_UNIX,SOCK_STREAM,0,f));rng_client_set(f[0],0);assert(rng_client_step(now)>=0);assert(read(f[1],b,16)==16);memcpy(b,"RNG1",4);rng_put(b+12,7);memset(b+16,0xa5,1024);}
int main(void){int f[2];unsigned char b[1040],out[1024];unsigned int i;
 begin(f,b,100);assert(!rng_client_ready());memset(out,1,sizeof(out));assert(rng_client_random(out,32)<0);for(i=0;i<32;i++)assert(!out[i]);
 assert(write(f[1],b,15)==15);assert(rng_client_step(101)>=0);assert(!rng_client_ready());assert(write(f[1],b+15,1025)==1025);assert(rng_client_step(102)==1);assert(rng_client_ready());assert(!rng_client_random(out,1024));for(i=0;i<1024;i++)assert(out[i]==0xa5);
 assert(rng_client_step(103)>=0);assert(read(f[1],b,16)==16);rng_put(b+12,8);memset(b+16,0,1024);assert(write(f[1],b,1040)==1040);assert(rng_client_step(104)<0);assert(rng_client_random(out,1024)<0);for(i=0;i<1024;i++)assert(!out[i]);close(f[1]);
 begin(f,b,100);assert(rng_client_step(3100)>=0);assert(!rng_client_ready());assert(rng_client_step(30099)>=0);assert(rng_client_step(30100)<0);close(f[1]);
 begin(f,b,100);assert(write(f[1],b,1)==1);assert(rng_client_step(4500)>=0);assert(rng_client_step(7499)>=0);assert(rng_client_step(7500)<0);close(f[1]);
 begin(f,b,100);rng_put(b+4,9);assert(write(f[1],b,1040)==1040);assert(rng_client_step(101)<0);close(f[1]);
 begin(f,b,100);close(f[1]);assert(rng_client_step(101)<0);
 rng_client_close();puts("RNG client: cached nonblocking, partial I/O, epoch revocation, stale sequence, timeout, EOF, clear-on-error PASS");return 0;
}
