/* Separate diagnostic, not a production provider. Consumes keypad queue.
 * Requires explicit hardware authorization and exclusive keypad ownership. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
int main(int argc,char **argv)
{
    int fd,n,key,r,last=-999;unsigned int i;struct timespec now,delay={0,10000000};
    if(argc!=2||strcmp(argv[1],"--exclusive-keypad-authorized")) {
        fputs("Requires authorized exclusive keypad ownership; no Gateway stop is performed.\n",stderr);return 2;
    }
    alarm(65);
    fd=open("/dev/keypad",O_RDONLY|O_NONBLOCK);if(fd<0){perror("keypad");return 1;}
    puts("monotonic_s.ns,has_press,get_key; absence is not release");
    for(i=0;i<6000U;++i) {
        n=-1;key=-999;r=ioctl(fd,1,&n);
        if(r<0){perror("HAS_PRESS");close(fd);return 1;}
        if(n>0&&ioctl(fd,2,&key)<0){perror("GET_KEY");close(fd);return 1;}
        if(clock_gettime(CLOCK_MONOTONIC,&now)){close(fd);return 1;}
        if(n!=last||n>0)printf("%lu.%09lu,%d,%d\n",(unsigned long)now.tv_sec,(unsigned long)now.tv_nsec,n,key);
        last=n;nanosleep(&delay,NULL);
    }
    close(fd);return 0;
}
