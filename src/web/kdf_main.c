#define _GNU_SOURCE
#include "web/exec_fds.h"
#include "web/kdf_worker.h"
#include "web/web_security.h"
#include "mbedtls/pkcs5.h"
#include "version.h"
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "web/kdf_cached.h"
#include <stdlib.h>
#include <sys/stat.h>
#ifdef WEB_HOST_TEST
int __real_fsync(int);
int __wrap_fsync(int fd){struct stat s;if(getenv("WEB_COMMIT_DIRSYNC_FAIL")&&!fstat(fd,&s)&&S_ISDIR(s.st_mode)){errno=EIO;return -1;}return __real_fsync(fd);}
#endif
static void clear(void*p,size_t n){volatile unsigned char*b=p;while(n--)*b++=0;}
static int commit(void){unsigned char b[KDF_COMMIT_SIZE];size_t at=0;ssize_t n;char salt[33],hash[65],record[160];int r=1;
 while(at<sizeof(b)){n=read(3,b+at,sizeof(b)-at);if(n<=0)goto done;at+=(size_t)n;}
 if(memcmp(b,"KDC1",4)||b[4]!='/'||!memchr(b+4,0,512))goto done;
 web_hex(b+516,16,salt);web_hex(b+532,32,hash);
 snprintf(record,sizeof(record),"salt=%s\nhash=%s\n",salt,hash);
 {int result=web_atomic_file((char*)b+4,record,strlen(record));if(result){r=result==-2?5:4;goto done;}}
 if(send(3,b+532,32,MSG_NOSIGNAL)!=32)goto done;r=0;
done:clear(b,sizeof(b));clear(record,sizeof(record));clear(salt,sizeof(salt));clear(hash,sizeof(hash));return r;
}
int main(int argc,char**argv){unsigned char b[KDF_WIRE_SIZE],hash[32];size_t at=0;ssize_t n;int r=1,nice;struct rlimit cpu={60,60},core={0,0};(void)argv;
 if(argc!=1&&(argc!=2||strcmp(argv[1],"commit"))){fprintf(stderr,"4VRS KDF %s: invalid invocation\n",FOURVRS_VERSION);return 2;}
 if(web_exec_close_from(4))return 1;signal(SIGPIPE,SIG_IGN);alarm(60);if(setrlimit(RLIMIT_CORE,&core)||setrlimit(RLIMIT_CPU,&cpu)||setpriority(PRIO_PROCESS,0,10))goto done;errno=0;nice=getpriority(PRIO_PROCESS,0);if(errno||nice<10)goto done;
 if(argc==2)return commit();
 while(at<sizeof(b)){n=read(3,b+at,sizeof(b)-at);if(n<=0)goto done;at+=(size_t)n;}
 if(memcmp(b,"KDF1",4)||b[4]>1||b[5]<12||b[5]>128||(b[4]&&(b[6]<12||b[6]>128))||b[7])goto done;
 if(b[4]){if(cached_pbkdf2(b+8,b[5],b+264,WEB_PASSWORD_ROUNDS,hash))goto done;
  {unsigned int i,d=0;for(i=0;i<32;i++)d|=hash[i]^b[296+i];if(d){r=2;goto done;}}
 }
 if(cached_pbkdf2(b+(b[4]?136:8),b[b[4]?6:5],b+(b[4]?280:264),WEB_PASSWORD_ROUNDS,hash))goto done;
 if(send(3,hash,32,MSG_NOSIGNAL)!=32)goto done;r=0;
done:clear(b,sizeof(b));clear(hash,sizeof(hash));return r;
}
