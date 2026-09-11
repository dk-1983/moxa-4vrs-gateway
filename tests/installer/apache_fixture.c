/* Private Linux-container fixture. Implements only the exact vendor argv. */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#define PIDFILE "/tmp/4vrs-apache-fixture.pid"
static void finish(int signal_number){(void)signal_number;unlink(PIDFILE);_exit(0);}
int main(int argc,char **argv){
 int fd,pipefd[2];pid_t child;FILE *file;struct sockaddr_in address={0};char ready;
 if(argc!=5||strcmp(argv[1],"-f")||strcmp(argv[2],"/etc/apache/httpd.conf")||strcmp(argv[3],"-k"))return 2;
 if(!strcmp(argv[4],"stop")){long pid;if(!access("/tmp/4vrs-apache-stop-failure",F_OK))return 3;file=fopen(PIDFILE,"r");if(!file)return 1;if(fscanf(file,"%ld",&pid)!=1||pid<=1)return 2;fclose(file);return kill((pid_t)pid,SIGTERM)!=0;}
 if(strcmp(argv[4],"start")||!access(PIDFILE,F_OK)||pipe(pipefd))return 2;
 child=fork();if(child<0)return 2;
 if(child){close(pipefd[1]);if(read(pipefd[0],&ready,1)!=1)return 2;close(pipefd[0]);return ready!='R';}
 close(pipefd[0]);setsid();signal(SIGTERM,finish);signal(SIGINT,finish);alarm(60);
 fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 3;
 address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(80);
 if(bind(fd,(struct sockaddr*)&address,sizeof(address))||listen(fd,1))return 3;
 file=fopen(PIDFILE,"wx");if(!file)return 4;fprintf(file,"%ld\n",(long)getpid());fclose(file);
 if(write(pipefd[1],"R",1)!=1)return 5;
 close(pipefd[1]);
 for(;;)pause();
}
