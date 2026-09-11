/* Execute the actual receiver audit with synthetic proc operations. No devices. */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/sysmacros.h>
static int scenario, position, fd_position;
static dev_t observed;
static DIR *fake_open(const char *path) {
 if(!strcmp(path,"/proc")){position=0;if(scenario==1){errno=EACCES;return NULL;}return (DIR*)1;}
 if(scenario==2){errno=EACCES;return NULL;}
 fd_position=0;return (DIR*)2;
}
static struct dirent *fake_read(DIR *d) {
 static struct dirent entry;
 if(d==(DIR*)1){if(position++){if(scenario==3)errno=EIO;return NULL;}strcpy(entry.d_name,"999999");}
 else {if(fd_position++){if(scenario==4)errno=EIO;return NULL;}strcpy(entry.d_name,"0");}
 return &entry;
}
static int fake_close(DIR *d){(void)d;return 0;}
static int fake_stat(const char *p,struct stat *s){(void)p;if(scenario==5){errno=EACCES;return -1;}if(scenario==6){errno=ENOENT;return -1;}
 memset(s,0,sizeof(*s));s->st_mode=S_IFCHR|0600;s->st_rdev=observed;return 0;}
#define opendir fake_open
#define readdir fake_read
#define closedir fake_close
#define stat(p,s) fake_stat(p,s)
#define main unused_receiver_main
#include "web/rng_bootstrap.c"
#undef main
static int checks;
static void test(const char *label,dev_t fd,int fault,int refused){observed=fd;scenario=fault;
 assert((sole_reader(makedev(4,65))!=0)==refused);checks++;printf("PASS %s\n",label);}
int main(void){
 test("direct foreign ttyS0 is unrelated",makedev(4,64),0,0);
 test("direct competitor ttyS1 refused",makedev(4,65),0,1);
 test("console alias even with reported ttyS0 remains unresolved",makedev(5,1),0,1);
 test("console alias routed ttyS1 refused",makedev(5,1),0,1);
 test("dev tty current controlling tty differs: open-time fd still unresolved",makedev(5,0),0,1);
 test("dev tty same controlling tty refused",makedev(5,0),0,1);
 test("tty0 routing unresolved",makedev(4,0),0,1);
 test("console routing changes between audits cannot grant access",makedev(5,1),0,1);
 test("proc root audit denied",makedev(4,64),1,1);
 test("pid fd audit denied",makedev(4,64),2,1);
 test("proc readdir error",makedev(4,64),3,1);
 test("fd readdir error",makedev(4,64),4,1);
 test("stat permission error",makedev(4,64),5,1);
 test("vanished fd is absent at that snapshot only",makedev(4,65),6,0);
 test("competitor appearing at next audit refused",makedev(4,65),0,1);
 printf("TOTAL %d PASS; unresolved aliases remain denied\n",checks);return 0;
}
