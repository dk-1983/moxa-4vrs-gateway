#define _GNU_SOURCE
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network/gateway_network_runtime.h"
/* Include the real private poller; only process/pipe syscalls are scripted. */
#include "network/gateway_network_runtime.c"
static gateway_network_service_status_t frame;
static int reads[8],ri,wi,waits[8],exit_status,kills,closes;
static core_tick_t tick=100;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
ssize_t __real_read(int,void*,size_t);
ssize_t __wrap_read(int fd,void *p,size_t n){int r;if(fd!=1001)return __real_read(fd,p,n);CHECK(ri<8);r=reads[ri++];if(r<0){errno=-r;return -1;}if(r){CHECK((size_t)r<=n);memcpy(p,&frame,(size_t)r);}return r;}
pid_t __real_waitpid(pid_t,int*,int);
pid_t __wrap_waitpid(pid_t p,int *s,int flags){int r;if(p!=123456)return __real_waitpid(p,s,flags);CHECK(flags==WNOHANG&&wi<8);r=waits[wi++];*s=exit_status;if(r<0){errno=-r;return -1;}return r?p:0;}
int __real_kill(pid_t,int);
int __wrap_kill(pid_t p,int sig){if(p!=123456)return __real_kill(p,sig);CHECK(sig==SIGKILL);++kills;return 0;}
int __real_close(int);
int __wrap_close(int fd){if(fd!=1001)return __real_close(fd);++closes;return 0;}
static core_tick_t clock_(void *c){(void)c;return tick;}
static int unused_observe(void *c,gateway_network_observation_t *o){(void)c;(void)o;CHECK(0);return -1;}
static gateway_network_environment_t env;
static void setup(gateway_network_runtime_t *r){memset(r,0,sizeof(*r));memset(&frame,0,sizeof(frame));memset(reads,0,sizeof(reads));memset(waits,0,sizeof(waits));ri=wi=kills=closes=exit_status=0;tick=100;env.clock=clock_;env.read_network=unused_observe;r->environment=&env;r->observer_fd=1001;r->observer_pid=123456;r->observer_deadline=300;r->observation_epoch=r->observer_epoch=5;frame.observed.lan[0].up=frame.observed.lan[0].link=1;strcpy(frame.observed.lan[0].address,"10.0.2.13");frame.lease_valid[0]=1;}
int main(void){gateway_network_runtime_t r;
 setup(&r);reads[0]=sizeof(frame);waits[1]=1;poll_observer(&r);CHECK(r.observed_valid&&!r.observation_error&&r.observer_pid);poll_observer(&r);CHECK(!r.observation_error&&r.lease_valid[0]&&closes==1);puts("PASS data before child exit survives later EOF");
 setup(&r);reads[0]=sizeof(frame);waits[0]=1;poll_observer(&r);CHECK(r.observed_valid&&!r.observation_error);puts("PASS data and successful exit in one poll");
 setup(&r);reads[0]=-EAGAIN;reads[1]=sizeof(frame);waits[0]=1;poll_observer(&r);CHECK(r.observed_valid&&!r.observation_error);puts("PASS data arrives between read and waitpid");
 setup(&r);waits[0]=1;poll_observer(&r);CHECK(!r.observed_valid&&r.observation_error);puts("PASS empty successful child refuses");
 setup(&r);reads[0]=3;waits[0]=1;poll_observer(&r);CHECK(!r.observed_valid&&r.observation_error);puts("PASS partial message refuses");
 setup(&r);++r.observation_epoch;reads[0]=sizeof(frame);waits[1]=1;poll_observer(&r);poll_observer(&r);CHECK(!r.observed_valid&&!r.observation_error&&r.observe_at==tick);puts("PASS stale epoch result/EOF does not poison current epoch");
 setup(&r);reads[0]=-EAGAIN;reads[1]=sizeof(frame);tick=300;poll_observer(&r);CHECK(kills==1&&r.observation_error);waits[1]=1;poll_observer(&r);CHECK(r.observation_error&&!r.lease_valid[0]);puts("PASS timeout cannot be undone by late snapshot");
 setup(&r);reads[0]=sizeof(frame);waits[0]=1;exit_status=1<<8;poll_observer(&r);CHECK(r.observation_error&&!r.lease_valid[0]);puts("PASS nonzero exit invalidates complete result");
 setup(&r);reads[0]=-EINTR;reads[1]=sizeof(frame);waits[0]=-EINTR;waits[1]=1;poll_observer(&r);CHECK(!r.observation_error);poll_observer(&r);CHECK(r.observed_valid&&!r.observation_error);puts("PASS EINTR retries without failure");
 setup(&r);reads[0]=sizeof(frame);waits[0]=1;frame.error=7;poll_observer(&r);CHECK(r.observation_error);puts("PASS owner error retained");
 setup(&r);reads[0]=sizeof(frame);waits[0]=1;frame.observed.unsupported=2;poll_observer(&r);CHECK(r.observation_error);puts("PASS unsupported snapshot retained");
 return 0;}
