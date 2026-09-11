#define _GNU_SOURCE
#include "web/rng_client.h"
#include "web/rng_wire.h"
#include "web/web_protocol.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
static struct {int fd,sync,failed;unsigned int sequence,epoch;size_t sent,used,total,count;uint32_t at;unsigned char tx[16],rx[1040],pool[1024];} c={-1,0,1,0,0,0,0,0,0,0,{0},{0},{0}};
void rng_client_close(void){if(c.fd>=0)close(c.fd);web_clear(&c,sizeof(c));c.fd=-1;c.failed=1;}
void rng_client_set(int fd,int sync){rng_client_close();c.fd=fd;c.sync=sync;c.failed=0;if(fd<0||web_nonblock(fd))rng_client_close();}
int rng_client_ready(void){return !c.failed&&c.count>=128&&!(c.total==16);}
unsigned int rng_client_epoch(void){return c.epoch;}
static int request(size_t n,uint32_t now){if(c.total||!n||n>RNG_MAX||c.sequence==0xffffffffU)return -1;c.sequence++;memcpy(c.tx,"RNG1",4);rng_put(c.tx+4,c.sequence);rng_put(c.tx+8,(unsigned int)n);rng_put(c.tx+12,0);c.sent=c.used=0;c.total=n+16;c.at=now;return 0;}
int rng_client_attach(int fd){struct msghdr m;struct iovec v;struct cmsghdr *h;union {struct cmsghdr align;unsigned char b[CMSG_SPACE(sizeof(int))];} control;ssize_t n;
 if(c.failed||c.total||c.sync||c.sequence==0xffffffffU)return -1;
 memset(&m,0,sizeof(m));memset(&control,0,sizeof(control));memcpy(c.tx,"RGA1",4);rng_put(c.tx+4,++c.sequence);rng_put(c.tx+8,0);rng_put(c.tx+12,0);c.used=c.sent=0;c.total=16;c.at=web_now();
 v.iov_base=c.tx;v.iov_len=16;m.msg_iov=&v;m.msg_iovlen=1;m.msg_control=control.b;m.msg_controllen=sizeof(control.b);h=CMSG_FIRSTHDR(&m);h->cmsg_level=SOL_SOCKET;h->cmsg_type=SCM_RIGHTS;h->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(h),&fd,sizeof(fd));
 n=sendmsg(c.fd,&m,MSG_NOSIGNAL);if(n<=0){rng_client_close();return -1;}c.sent=(size_t)n;return 0;
}
int rng_client_step(uint32_t now){ssize_t n;unsigned int epoch;
 if(c.failed)return -1;
 if(!c.total){if(c.sync||c.count>=128)return 0;if(request(1024-c.count,now))goto fail;}
 if(now-c.at>=((c.sent==16&&c.total>16&&!c.used)?RNG_COMMIT_WAIT_MS:RNG_FRAME_WAIT_MS))goto fail;
 if(c.sent<16){n=send(c.fd,c.tx+c.sent,16-c.sent,MSG_NOSIGNAL);if(n>0)c.sent+=(size_t)n;else if(!n||(errno!=EINTR&&errno!=EAGAIN))goto fail;}
 if(c.sent<16)return 0;
 n=recv(c.fd,c.rx+c.used,c.total-c.used,MSG_DONTWAIT);if(n>0){if(!c.used)c.at=now;c.used+=(size_t)n;}else if(!n||(errno!=EINTR&&errno!=EAGAIN))goto fail;
 if(c.used!=c.total)return 0;
 epoch=rng_get(c.rx+12);
 if(memcmp(c.rx,"RNG1",4)||rng_get(c.rx+4)!=c.sequence||rng_get(c.rx+8)!=c.total-16||!epoch||(c.epoch&&c.epoch!=epoch)||c.count+c.total-16>1024)goto fail;
 c.epoch=epoch;memcpy(c.pool+c.count,c.rx+16,c.total-16);c.count+=c.total-16;
 web_clear(c.rx,sizeof(c.rx));web_clear(c.tx,sizeof(c.tx));c.used=c.sent=c.total=0;return 1;
fail:
#ifdef WEB_HOST_TEST
 fprintf(stderr,"rng-client-fail sent=%lu used=%lu total=%lu errno=%d\n",(unsigned long)c.sent,(unsigned long)c.used,(unsigned long)c.total,errno);
#endif
 rng_client_close();return -1;
}
int rng_client_random(void *out,size_t n){
 if(c.failed||!n||n>1024)goto fail;
 if(c.sync&&c.count<n){uint32_t start=web_now();if(request(n-c.count,start))goto fail;while(c.total){fd_set r,w;struct timeval t={0,10000};if(rng_client_step(web_now())<0)goto fail;if(!c.total)break;FD_ZERO(&r);FD_ZERO(&w);if(c.fd<0||c.fd>=FD_SETSIZE)goto fail;FD_SET(c.fd,&r);if(c.sent<16)FD_SET(c.fd,&w);if(select(c.fd+1,&r,&w,NULL,&t)<0&&errno!=EINTR)goto fail;}}
 if(c.count<n)goto fail;

#ifdef WEB_HOST_TEST
 fprintf(stderr,"rng-consumer mode=%s bytes=%lu\n",c.sync?"web":"gateway",(unsigned long)n);
#endif
 memcpy(out,c.pool,n);memmove(c.pool,c.pool+n,c.count-n);c.count-=n;web_clear(c.pool+c.count,n);return 0;
fail:web_clear(out,n);return -1;
}
