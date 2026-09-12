#ifndef FOURVRS_NETWORK_FRAMES_H
#define FOURVRS_NETWORK_FRAMES_H
/* Linux 2.4 has no UNIX SEQPACKET. Each direction has a fixed wire record:
 * command_t toward owner, service_status toward client. A single reader owns
 * each fd. Peek leaves incomplete records queued; consume exactly one record.
 * now_ms(), poll/socket/errno/string declarations are supplied by the caller. */
#include "core/platform.h"
#ifdef FOURVRS_NETWORK_STREAM_IPC
#define SERVICE_SOCKET_TYPE SOCK_STREAM
#define GUARDIAN_SOCKET_TYPE SOCK_DGRAM
static ssize_t service_receive(int fd,void *record,size_t size)
{
    ssize_t n;struct pollfd p;
    n=recv(fd,record,size,MSG_PEEK|MSG_DONTWAIT);
    if(n<=0)return n;
    if((size_t)n<size){
        p.fd=fd;p.events=POLLIN;p.revents=0;
        if(poll(&p,1,0)<0)return -1;
        if(p.revents&(POLLHUP|POLLERR|POLLNVAL)){errno=EPROTO;return -1;}
        errno=EAGAIN;return -1;
    }
    n=recv(fd,record,size,MSG_DONTWAIT);
    if(n!=(ssize_t)size){(void)shutdown(fd,SHUT_RDWR);errno=EPROTO;return -1;}
    return n;
}
static ssize_t service_send(int fd,const void *record,size_t size)
{
    size_t sent=0;core_tick_t began=now_ms();
    while(sent<size){
        ssize_t n=send(fd,(const char *)record+sent,size-sent,MSG_DONTWAIT|MSG_NOSIGNAL);
        if(n>0){sent+=(size_t)n;continue;}
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)){
            struct pollfd p;int elapsed=(int32_t)(now_ms()-began);
            if(elapsed>=50)break;
            p.fd=fd;p.events=POLLOUT;p.revents=0;
            if(poll(&p,1,50-elapsed)<0&&errno!=EINTR)break;
            if(p.revents&(POLLHUP|POLLERR|POLLNVAL))break;
            continue;
        }
        break;
    }
    if(sent==size)return (ssize_t)sent;
    /* Never permit a later send to append a new frame behind a partial one. */
    (void)shutdown(fd,SHUT_RDWR);errno=EPIPE;return -1;
}
#else
#define SERVICE_SOCKET_TYPE SOCK_SEQPACKET
#define GUARDIAN_SOCKET_TYPE SOCK_SEQPACKET
static ssize_t service_receive(int fd,void *record,size_t size)
{return recv(fd,record,size,MSG_DONTWAIT|MSG_TRUNC);}
static ssize_t service_send(int fd,const void *record,size_t size)
{return send(fd,record,size,MSG_NOSIGNAL);}
#endif
static ssize_t service_receive_wait(int fd,void *record,size_t size,unsigned int timeout)
{
    core_tick_t began=now_ms();
    for(;;){
        ssize_t n=service_receive(fd,record,size);
        if(n>=0||!(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return n;
        if((uint32_t)(now_ms()-began)>=timeout){errno=ETIMEDOUT;return -1;}
        /* An incomplete stream record stays readable. Bound retry CPU too. */
        usleep(1000);
    }
}
#endif
