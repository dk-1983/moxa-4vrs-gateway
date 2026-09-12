#define _GNU_SOURCE
#include "web/web_gateway.h"
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
static uint32_t ticks;
uint32_t __wrap_web_now(void){ticks+=10000;return ticks;}
int __wrap_usleep(useconds_t n){(void)n;return 0;}
static void init(web_gateway_t*w){memset(w,0,sizeof(*w));w->initialized=1;w->peer=-1;w->listener=-1;w->rng_web=-1;w->rng_pending_fd=-1;}
int main(void){web_gateway_t w;int p[2],status;pid_t child;char b;
 init(&w);assert(web_gateway_close(&w)==0);
 assert(pipe(p)==0);child=fork();assert(child>=0);
 if(!child){close(p[0]);signal(SIGTERM,SIG_IGN);write(p[1],"x",1);for(;;)pause();}
 close(p[1]);assert(read(p[0],&b,1)==1);close(p[0]);init(&w);w.rng_pid=child;ticks=0;
 assert(web_gateway_close(&w)!=0);assert(w.rng_pid==child);assert(kill(child,0)==0);
 kill(child,SIGKILL);assert(waitpid(child,&status,0)==child);
 init(&w);w.rng_pid=child;assert(web_gateway_close(&w)!=0);
 puts("shutdown close: empty, bounded timeout, live RNG preserved, wait failure PASS");return 0;}
