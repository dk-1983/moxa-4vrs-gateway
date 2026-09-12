#define _GNU_SOURCE
#include "core/monotonic.h"
#include "web/rng_client.h"
#include "web/web_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
extern int fsync(int);
uint32_t web_now(void){struct timespec t;if(gateway_monotonic_time(&t))return 0;return (uint32_t)t.tv_sec*1000U+(uint32_t)t.tv_nsec/1000000U;}
int web_nonblock(int fd){return fcntl(fd,F_SETFL,O_NONBLOCK)||fcntl(fd,F_SETFD,FD_CLOEXEC)?-1:0;}
void web_clear(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
int web_equal(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;unsigned int v=0;while(n--)v|=*x++^*y++;return v==0;}
int web_random(void *p,size_t n){return rng_client_random(p,n);}
void web_hex(const unsigned char *p,size_t n,char *out){static const char h[]="0123456789abcdef";size_t i;for(i=0;i<n;++i){out[2*i]=h[p[i]>>4];out[2*i+1]=h[p[i]&15];}out[2*n]=0;}
static int hexchar(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
int web_unhex(const char *s,unsigned char *out,size_t n){size_t i;if(strlen(s)!=2*n)return -1;for(i=0;i<n;++i){int a=hexchar(s[2*i]),b=hexchar(s[2*i+1]);if(a<0||b<0)return -1;out[i]=(unsigned char)(a*16+b);}return 0;}
int web_frame_feed(web_frame_t *f,const void *p,size_t n){unsigned int length;if(n>sizeof(f->bytes)-f->used)return -1;memcpy(f->bytes+f->used,p,n);f->used+=n;if(f->used<8)return 0;if(memcmp(f->bytes,"4V\1\0",4))return -1;length=((unsigned int)f->bytes[4]<<24)|((unsigned int)f->bytes[5]<<16)|((unsigned int)f->bytes[6]<<8)|f->bytes[7];if(!length||length>WEB_BODY_MAX)return -1;f->total=length+8U;if(f->used>f->total)return -1;return f->used==f->total?1:0;}
int web_frame_make(web_frame_t *f,const char *s){size_t n=strlen(s);if(!n||n>WEB_BODY_MAX)return -1;memset(f,0,sizeof(*f));memcpy(f->bytes,"4V\1\0",4);f->bytes[6]=(unsigned char)(n>>8);f->bytes[7]=(unsigned char)n;memcpy(f->bytes+8,s,n);f->used=f->total=n+8;return 0;}
int web_read_file(const char *path,void *out,size_t cap,size_t *n){int fd;ssize_t r;struct stat st;fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return errno==ENOENT?1:-1;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(size_t)st.st_size>=cap){close(fd);return -1;}r=read(fd,out,cap);close(fd);if(r!=st.st_size)return -1;*n=(size_t)r;((char*)out)[*n]=0;return 0;}
int web_atomic_file(const char *path,const void *data,size_t n){char tmp[512],dir[512],*slash;int fd,dfd;size_t at=0;ssize_t r;if(strlen(path)>480)return -1;
#ifndef WEB_HOST_TEST
{struct stat a,b;if(strncmp(path,"/var/hda/",9)||stat("/var",&a)||stat("/var/hda",&b)||a.st_dev==b.st_dev)return -1;}
#endif
snprintf(tmp,sizeof(tmp),"%s.next",path);strcpy(dir,path);slash=strrchr(dir,'/');if(!slash)return -1;*slash=0;dfd=open(dir,O_RDONLY);if(dfd<0)return -1;fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0&&errno==EEXIST){struct stat stale;/* Single writer in a private directory: discard an interrupted, unpublished generation. */if(!lstat(tmp,&stale)&&S_ISREG(stale.st_mode)&&stale.st_uid==geteuid()&&stale.st_nlink==1&&!(stale.st_mode&077)&&!unlink(tmp))fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);}if(fd<0){close(dfd);return -1;}while(at<n){r=write(fd,(const char*)data+at,n-at);if(r<=0)break;at+=(size_t)r;}if(at!=n||fsync(fd)){close(fd);unlink(tmp);close(dfd);return -1;}if(close(fd)||rename(tmp,path)){unlink(tmp);close(dfd);return -1;}r=fsync(dfd);close(dfd);return r?-2:0;}
const char *web_field(const web_fields_t *f,const char *key){unsigned int i;for(i=0;i<f->count;++i)if(!strcmp(f->key[i],key))return f->value[i];return "";}
int web_fields_parse(web_fields_t *f,const char *s,size_t n){size_t i=0,k,v;unsigned int j;memset(f,0,sizeof(*f));while(i<n){if(f->count==32)return -1;k=0;while(i<n&&s[i]!='='){char c=s[i++];if(k==31||!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return -1;f->key[f->count][k++]=c;}if(!k||i==n)return -1;++i;for(j=0;j<f->count;++j)if(!strcmp(f->key[j],f->key[f->count]))return -1;v=0;while(i<n&&s[i]!='\n'){int c=(unsigned char)s[i++];if(c=='%'){int a,b;if(i+1>=n)return -1;a=hexchar(s[i++]);b=hexchar(s[i++]);if(a<0||b<0)return -1;c=a*16+b;}if(c<32||c==127||v==255)return -1;f->value[f->count][v++]=(char)c;}if(i==n)return -1;++i;++f->count;}return 0;}
int web_fields_only(const web_fields_t *f,const char *allowed){unsigned int i;char name[36];for(i=0;i<f->count;++i){snprintf(name,sizeof(name),"|%s|",f->key[i]);if(!strstr(allowed,name))return -1;}return 0;}
int web_number(const char *s,unsigned int max,unsigned int *out){unsigned int v=0,d;if(!*s)return -1;while(*s){if(*s<'0'||*s>'9')return -1;d=(unsigned int)(*s++-'0');if(d>max||v>(max-d)/10U)return -1;v=v*10U+d;}*out=v;return 0;}
