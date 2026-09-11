/* Host-only deterministic socket faults; never linked into target payload. */
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
ssize_t __real_send(int,const void*,size_t,int);
ssize_t __wrap_send(int fd,const void *data,size_t n,int flags){
 struct sockaddr_storage address;socklen_t size=sizeof(address);static unsigned int calls;
 if(!getsockname(fd,(struct sockaddr*)&address,&size)&&address.ss_family==AF_INET){
  ++calls;if(calls%5==0){errno=EAGAIN;return -1;}if(calls%13==0){errno=EINTR;return -1;}
  if(n>97)n=97;
 }
 return __real_send(fd,data,n,flags);
}
