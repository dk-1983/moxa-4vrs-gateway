#define _GNU_SOURCE
#include "installer/install_apache.h"
#include "installer/install_process.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int install_apache_plan(const char *root,const char *init,install_plan_t *plan){
 install_file_t gate={0,0,0,0},entry={0,0,0,0},absent={0,0,0,0};char path[256];int r=-1;
 if(install_file_read(root,"etc/rc.d/rcS.d/S21apache",&gate))return -1;
 if(!gate.kind)return 0;
 /* Captured vendor symlink. Unknown replacements are not disabled by name. */
 if(gate.kind!=2||gate.size!=16||memcmp(gate.data,"../init.d/apache",16))goto done;
 snprintf(path,sizeof(path),"%s/apache",init);
 if(install_file_read(root,path,&entry)||entry.kind!=2||entry.size!=24||memcmp(entry.data,"../../usr/sbin/apachectl",24))goto done;
 r=install_plan_add(plan,root,"etc/rc.d/rcS.d/S21apache",0,&absent);
done:install_file_free(&gate);install_file_free(&entry);return r;
}
static int scan(unsigned int *running){
 DIR *dir;struct dirent *e;struct stat binary;unsigned int seen=0,count=0;int present=!stat("/bin/httpd",&binary);
 *running=0;dir=opendir("/proc");if(!dir)return -1;
 while((e=readdir(dir))!=NULL){char *end,path[128],args[256];long pid=strtol(e->d_name,&end,10);struct stat exe;int fd;ssize_t n;install_process_t identity;
  if(*end||pid<=1)continue;
  if(++seen>4096){closedir(dir);return -1;}
  snprintf(path,sizeof(path),"/proc/%ld/exe",pid);if(stat(path,&exe))continue;
  if(!present||exe.st_ino!=binary.st_ino||exe.st_dev!=binary.st_dev)continue;
  snprintf(path,sizeof(path),"/proc/%ld/cmdline",pid);fd=open(path,O_RDONLY);if(fd<0){closedir(dir);return -1;}n=read(fd,args,sizeof(args));close(fd);
  {static const char expected[]="/bin/httpd\0-f\0/etc/apache/httpd.conf\0-k\0start\0";
   if(n!=(ssize_t)sizeof(expected)-1||memcmp(args,expected,sizeof(expected)-1)||install_process_capture((pid_t)pid,"/bin/httpd",&identity)){closedir(dir);return -1;}}
  if(++count>64){closedir(dir);return -1;}
 }
 closedir(dir);*running=count?1U:0U;return 0;
}
int install_apache_inspect(unsigned int *running){return scan(running);}
static int owned_socket(unsigned long inode){
 DIR *procs=opendir("/proc");struct dirent *e;unsigned int seen=0;int result=0;
 if(!procs)return -1;
 while((e=readdir(procs))!=NULL){char *end,path[128],link[128],expected[64];long pid=strtol(e->d_name,&end,10);DIR *fds;struct dirent *f;install_process_t owner;unsigned int count=0;int trusted;
  if(*end||pid<=1)continue;
  if(++seen>4096){result=-1;break;}
  trusted=!install_process_capture((pid_t)pid,"/bin/httpd",&owner);
  if(!trusted&&!install_process_capture((pid_t)pid,"/var/hda/4vrs/bin/4vrs-web",&owner)){
   char statbuf[2048],*closing;long parent;int fd;ssize_t n;install_process_t gateway;
   snprintf(path,sizeof(path),"/proc/%ld/stat",pid);fd=open(path,O_RDONLY);if(fd<0)continue;n=read(fd,statbuf,sizeof(statbuf)-1);close(fd);if(n<=0)continue;statbuf[n]=0;closing=strrchr(statbuf,')');
   if(closing&&sscanf(closing+1," %*c %ld",&parent)==1&&parent>1&&!install_process_capture((pid_t)parent,"/var/hda/4vrs/bin/4vrs-gateway",&gateway))trusted=1;
  }
  if(!trusted)continue;
  snprintf(path,sizeof(path),"/proc/%ld/fd",pid);fds=opendir(path);if(!fds)continue;snprintf(expected,sizeof(expected),"socket:[%lu]",inode);
  while((f=readdir(fds))!=NULL){ssize_t n;char *tail;long descriptor=strtol(f->d_name,&tail,10);
   if(*tail||descriptor<0)continue;
   if(++count>4096){result=-1;break;}
   snprintf(path,sizeof(path),"/proc/%ld/fd/%ld",pid,descriptor);n=readlink(path,link,sizeof(link)-1);if(n<0)continue;link[n]=0;
   if(!strcmp(link,expected)&&install_process_matches(&owner)==1){result=1;break;}
  }
  closedir(fds);if(result)break;
 }
 closedir(procs);return result;
}
int install_apache_ports(void){
 const char *names[]={"/proc/net/tcp","/proc/net/tcp6"};unsigned int i;
 for(i=0;i<2;i++){FILE *file=fopen(names[i],"r");char line[1024];unsigned int rows=0;
  if(!file){if(i&&errno==ENOENT)continue;return -1;}
  if(!fgets(line,sizeof(line),file)){fclose(file);return -1;}
  while(fgets(line,sizeof(line),file)){char *fields[10],*save=0,*part,*colon,*end;unsigned int n=0;unsigned long port,state,inode;
   if(++rows>4096||!strchr(line,'\n')){fclose(file);return -1;}
   part=strtok_r(line," \t\r\n",&save);while(part&&n<10){fields[n++]=part;part=strtok_r(0," \t\r\n",&save);}if(n<10){fclose(file);return -1;}
   state=strtoul(fields[3],&end,16);if(*end){fclose(file);return -1;}if(state!=10)continue;
   colon=strrchr(fields[1],':');if(!colon){fclose(file);return -1;}port=strtoul(colon+1,&end,16);if(*end){fclose(file);return -1;}if(port!=80&&port!=443)continue;
   inode=strtoul(fields[9],&end,10);if(*end||!inode||owned_socket(inode)!=1){fclose(file);return -1;}
  }
  if(ferror(file)){fclose(file);return -1;}fclose(file);
 }
 return 0;
}
static int command(const char *action){char *argv[]={"/bin/httpd","-f","/etc/apache/httpd.conf","-k",0,0};argv[4]=(char*)action;return install_process_run(argv[0],argv,10000);}
int install_apache_stop(void){unsigned int running,i;if(scan(&running))return -1;if(!running)return 0;if(command("stop"))return -1;for(i=0;i<100;i++){if(scan(&running))return -1;if(!running)return 0;usleep(100000);}return -1;}
int install_apache_restore(unsigned int previous){unsigned int running,i;if(scan(&running))return -1;if(!previous)return running?install_apache_stop():0;if(running)return 0;if(command("start"))return -1;for(i=0;i<100;i++){if(scan(&running))return -1;if(running)return 0;usleep(100000);}return -1;}
