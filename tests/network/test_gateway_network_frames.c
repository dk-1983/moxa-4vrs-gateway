#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
typedef uint32_t core_tick_t;
static core_tick_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (core_tick_t)(t.tv_sec*1000UL+t.tv_nsec/1000000UL);}
#include "network/gateway_network_frames.h"
static unsigned int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno);exit(1);}}while(0)
int main(void){
 int p[2],status;char data[12],joined[24],out[12];pid_t child;core_tick_t began;
 memset(data,42,sizeof(data));memcpy(joined,data,12);memset(joined+12,43,12);
 CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,p));
 CHECK(send(p[0],data,3,0)==3);CHECK(service_receive(p[1],out,12)<0&&errno==EAGAIN);
 CHECK(send(p[0],data+3,9,0)==9);CHECK(service_receive(p[1],out,12)==12);CHECK(!memcmp(data,out,12));
 CHECK(send(p[0],joined,24,0)==24);CHECK(service_receive(p[1],out,12)==12);CHECK(out[0]==42);
 CHECK(service_receive(p[1],out,12)==12);CHECK(out[0]==43);
 child=fork();CHECK(child>=0);
 if(!child){close(p[1]);usleep(15000);(void)send(p[0],data,5,0);usleep(15000);(void)send(p[0],data+5,7,0);_exit(0);}
 CHECK(service_receive_wait(p[1],out,12,100)==12);CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
 began=now_ms();CHECK(service_receive_wait(p[1],out,12,20)<0&&errno==ETIMEDOUT);CHECK(now_ms()-began<200);
 CHECK(service_send(p[0],data,12)==12);CHECK(service_receive(p[1],out,12)==12);
 CHECK(send(p[0],data,4,0)==4);close(p[0]);CHECK(service_receive(p[1],out,12)<0&&errno==EPROTO);close(p[1]);
 CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,p));
 {int bytes=1024;char *large=malloc(1024*1024);CHECK(large!=0);memset(large,1,1024*1024);CHECK(!setsockopt(p[0],SOL_SOCKET,SO_SNDBUF,&bytes,sizeof(bytes)));
 began=now_ms();CHECK(service_send(p[0],large,1024*1024)<0);CHECK(now_ms()-began<500);CHECK(service_send(p[0],data,12)<0);free(large);}
 close(p[0]);close(p[1]);
 CHECK(!socketpair(AF_UNIX,GUARDIAN_SOCKET_TYPE,0,p));CHECK(send(p[0],"H",1,0)==1);CHECK(send(p[0],data,12,0)==12);
 CHECK(recv(p[1],out,12,0)==1&&out[0]=='H');CHECK(recv(p[1],out,12,0)==12);close(p[0]);close(p[1]);
 printf("network frames: %u checks PASS\n",checks);return 0;
}
