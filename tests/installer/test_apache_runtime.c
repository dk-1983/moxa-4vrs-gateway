/* Run only in the private qualification container. No device access. */
#define _GNU_SOURCE
#include "installer/install_apache.h"
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
int main(void){
 unsigned int running;int fd;struct sockaddr_in address={0};
 assert(!install_apache_inspect(&running)&&!running);
 assert(!install_apache_ports());
 assert(!install_apache_stop()); /* already stopped stays stopped */
 assert(!install_apache_restore(1));
 assert(!install_apache_inspect(&running)&&running);
 assert(!install_apache_ports()); /* fixture's real listening socket */
 assert(!install_apache_restore(1)); /* idempotent running restore */
 {FILE *failure=fopen("/tmp/4vrs-apache-stop-failure","wx");assert(failure);assert(!fclose(failure));
  assert(install_apache_stop()==-1);assert(!install_apache_inspect(&running)&&running);
  assert(!unlink("/tmp/4vrs-apache-stop-failure"));}
 assert(!install_apache_stop());
 assert(!install_apache_inspect(&running)&&!running);
 assert(!install_apache_restore(0));
 fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
 address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(80);
 assert(!bind(fd,(struct sockaddr*)&address,sizeof(address))&&!listen(fd,1));
 assert(install_apache_ports()==-1); /* unknown owner refused, remains alive */
 assert(!install_apache_stop());assert(getsockname(fd,(struct sockaddr*)&address,(socklen_t[]){sizeof(address)})==0);
 close(fd);puts("Apache runtime: stopped/running restore, owned listener, unknown listener refusal passed");return 0;
}
