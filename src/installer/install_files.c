#define _GNU_SOURCE
#include "installer/install_files.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int trusted_directory(const char *p){struct stat s;return lstat(p,&s)==0&&S_ISDIR(s.st_mode)&&s.st_uid==geteuid()&&!(s.st_mode&0022)?0:-1;}
/* Linux 2.6.10 has no openat. Anchor each directory by fd and operate on a
 * leaf relative to that fd's cwd. This API is intentionally single-threaded.
 * Renaming a checked parent cannot redirect a later open through a symlink. */
static int enter_parent(const char *root,const char *relative,int *saved,char leaf[256]){
 char path[1024],walk[1024],*part,*slash;int fd,next;struct stat s;
 if(install_path(root,relative,path))return -1;
 *saved=open(".",O_RDONLY|O_DIRECTORY);if(*saved<0)return -1;
 fd=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0){close(*saved);return -1;}
 strcpy(walk,path+1);part=walk;
 while((slash=strchr(part,'/'))!=0){
  *slash=0;
  if(fchdir(fd)){close(fd);goto fail;}
  next=open(part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);close(fd);fd=next;
  if(fd<0)goto fail;
  if(fstat(fd,&s)||!S_ISDIR(s.st_mode)||((size_t)(slash-walk)+1>=strlen(root)&&(s.st_uid!=geteuid()||(s.st_mode&0022)))){close(fd);goto fail;}
  part=slash+1;
 }
 if(strlen(part)>255||fchdir(fd)){close(fd);goto fail;}
 strcpy(leaf,part);return fd;
 fail:(void)fchdir(*saved);close(*saved);return -1;
}
static int leave_parent(int parent,int saved,int result){if(fchdir(saved))result=-1;if(close(parent))result=-1;if(close(saved))result=-1;return result;}
int install_path(const char *root,const char *rel,char out[1024]){
 size_t i,n,r;char parent[1024];
 if(!root||!rel||root[0]!='/'||!rel[0]||rel[0]=='/'||strlen(rel)>INSTALL_PATH_LIMIT)return -1;
 r=strlen(root);n=strlen(rel);if(r+n+2>=1024||trusted_directory(root))return -1;
 for(i=0;i<n;i++)if(!((rel[i]>='a'&&rel[i]<='z')||(rel[i]>='A'&&rel[i]<='Z')||(rel[i]>='0'&&rel[i]<='9')||strchr("/._-",rel[i])))return -1;
 if(!strcmp(rel,".")||!strcmp(rel,"..")||strstr(rel,"//")||strstr(rel,"/../")||strstr(rel,"/./")||!strncmp(rel,"../",3)||!strncmp(rel,"./",2)||rel[n-1]=='/'||(n>=3&&!strcmp(rel+n-3,"/.."))||(n>=2&&!strcmp(rel+n-2,"/.")))return -1;
 snprintf(out,1024,"%s%s%s",root,r==1?"":"/",rel);strcpy(parent,out);
 for(i=r+(r==1?0:1);parent[i];i++)if(parent[i]=='/'){parent[i]=0;if(trusted_directory(parent))return -1;parent[i]='/';}
 return 0;
}
int install_directory_sync(const char *path){int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW),r;if(fd<0)return -1;r=fsync(fd);if(close(fd))r=-1;return r;}
void install_file_free(install_file_t*f){free(f->data);memset(f,0,sizeof(*f));}
static int identity(const struct stat*a,const struct stat*b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_nlink==b->st_nlink&&a->st_mtime==b->st_mtime&&a->st_ctime==b->st_ctime;}
int install_file_read(const char *root,const char *relative,install_file_t*f){
 char p[256];struct stat a,b,c;int fd=-1,parent,saved;size_t pos=0;ssize_t n;unsigned char extra;
 memset(f,0,sizeof(*f));parent=enter_parent(root,relative,&saved,p);if(parent<0)return -1;
 if(lstat(p,&a))return leave_parent(parent,saved,errno==ENOENT?0:-1);
 if(a.st_uid!=geteuid()||a.st_size<0||(unsigned long)a.st_size>INSTALL_FILE_LIMIT)goto fail;
 f->mode=(unsigned int)(a.st_mode&0777);f->size=(size_t)a.st_size;
 if(S_ISLNK(a.st_mode)){
  if(f->size==0||f->size>INSTALL_PATH_LIMIT)goto fail;
  f->data=malloc(f->size+1);if(!f->data)goto fail;
  n=readlink(p,(char*)f->data,f->size+1);if(n!=(ssize_t)f->size||lstat(p,&b)||!identity(&a,&b))goto fail;
  f->data[f->size]=0;f->kind=2;return leave_parent(parent,saved,0);
 }
 if(!S_ISREG(a.st_mode)||a.st_nlink!=1||(a.st_mode&0022))goto fail;
 fd=open(p,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)goto fail;
 if(fstat(fd,&b)||!identity(&a,&b))goto fail;
 f->data=malloc(f->size+1);if(!f->data)goto fail;
 while(pos<f->size){n=read(fd,f->data+pos,f->size-pos);if(n<0&&errno==EINTR)continue;if(n<=0)goto fail;pos+=(size_t)n;}
 if(read(fd,&extra,1)!=0||fstat(fd,&b)||lstat(p,&c)||!identity(&a,&b)||!identity(&a,&c))goto fail;
 f->data[f->size]=0;f->kind=1;if(close(fd)){fd=-1;goto fail;}return leave_parent(parent,saved,0);
 fail:if(fd>=0)close(fd);install_file_free(f);return leave_parent(parent,saved,-1);
}
int install_file_equal(const install_file_t*a,const install_file_t*b){return a->kind==b->kind&&a->mode==b->mode&&a->size==b->size&&(!a->size||!memcmp(a->data,b->data,a->size));}
int install_file_publish(const char*root,const char*relative,const install_file_t*f){
 char path[1024],leaf[256],tmp[300];int fd=-1,r=-1,parent,saved;size_t pos=0;ssize_t n;install_file_t existing;
 if(f->kind>2||f->size>INSTALL_FILE_LIMIT||(f->size&&!f->data)||(f->mode&~0777U)||(f->kind==1&&(f->mode&0022))||install_path(root,relative,path))return -1;
 if(f->kind==2&&(!f->size||f->size>INSTALL_PATH_LIMIT||memchr(f->data,0,f->size)))return -1;
 if(install_file_read(root,relative,&existing))return -1;
 install_file_free(&existing);parent=enter_parent(root,relative,&saved,leaf);if(parent<0)return -1;
 if(!f->kind){r=unlink(leaf);if(r&&errno!=ENOENT)return leave_parent(parent,saved,-1);return leave_parent(parent,saved,fsync(parent));}
 /* mkstemp never opens or follows an attacker-selected temporary name. */
 snprintf(tmp,sizeof(tmp),"%s.install-XXXXXX",leaf);fd=mkstemp(tmp);if(fd<0)return leave_parent(parent,saved,-1);
 if(f->kind==1){
  while(pos<f->size){n=write(fd,f->data+pos,f->size-pos);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;pos+=(size_t)n;}
  if(fchmod(fd,(mode_t)f->mode)||fsync(fd))goto done;
  if(close(fd)){fd=-1;goto done;}fd=-1;
 }else{
  char target[INSTALL_PATH_LIMIT+1];memcpy(target,f->data,f->size);target[f->size]=0;
  if(close(fd)){fd=-1;goto done;}fd=-1;
  if(unlink(tmp)||symlink(target,tmp))goto done;
 }
 if(rename(tmp,leaf))goto done;
 r=fsync(parent);
 done:if(fd>=0)close(fd);if(r)unlink(tmp);return leave_parent(parent,saved,r);
}
