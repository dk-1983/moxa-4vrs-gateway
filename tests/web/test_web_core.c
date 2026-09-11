#define _GNU_SOURCE
#include "web/web_http.h"
#include "web/web_security.h"
#include "web/kdf_worker.h"
#include "web/web_certificate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
 #include <errno.h>
static int fail_directory_sync;
int __real_fsync(int);
int __wrap_fsync(int fd){struct stat st;if(fail_directory_sync&&!fstat(fd,&st)&&S_ISDIR(st.st_mode)){fail_directory_sync=0;errno=EIO;return -1;}return __real_fsync(fd);}
static int no_entropy,bad_time;static long time_shift;
int __real_web_random(void *,size_t);
int __wrap_web_random(void *p,size_t n){int fd;ssize_t got;if(no_entropy)return -1;fd=open("/dev/urandom",O_RDONLY);if(fd<0)return -1;got=read(fd,p,n);close(fd);return got==(ssize_t)n?0:-1;}
time_t __real_time(time_t *);
time_t __wrap_time(time_t *p){if(bad_time){if(p)*p=0;return 0;}time_t value=__real_time(NULL)+time_shift;if(p)*p=value;return value;}
static int finish(web_security_t *s){int r;uint32_t start=web_now();do{r=web_security_poll(s,web_now());assert(web_now()-start<10000);usleep(1000);}while(!r);return r;}
static void late_commit_tests(const char *dir){
 web_security_t s;char path[512];int r;uint32_t start;
 snprintf(path,sizeof(path),"%s/late-admin",dir);assert(!web_security_init(&s,path));
 assert(!web_security_local(&s,1,web_now()));assert(!web_security_begin(&s,1,s.code,"test-password-123",web_now()));
 start=web_now();while(!s.commit_pending){assert(web_security_poll(&s,web_now())==0);assert(web_now()-start<10000);usleep(1000);}
 /* Expiration after durable writer launch is ambiguous: no session and no
  * stale in-memory verifier may remain usable, even with a late success. */
 s.code_at=web_now()-WEB_CODE_MS;do{r=web_security_poll(&s,web_now());usleep(1000);}while(!r);
 assert(r==403&&s.save_failed&&!s.token[0]&&!s.csrf[0]&&!s.commit_pending);
 web_security_cancel(&s);
 snprintf(path,sizeof(path),"%s/cancel-admin",dir);assert(!web_security_init(&s,path));
 assert(!web_security_local(&s,1,web_now()));assert(!web_security_begin(&s,1,s.code,"test-password-123",web_now()));
 start=web_now();while(!s.commit_pending){assert(web_security_poll(&s,web_now())==0);assert(web_now()-start<10000);usleep(1000);}
 web_security_cancel_pending(&s);assert(s.save_failed&&!s.token[0]&&!s.csrf[0]&&!s.worker);assert(web_security_poll(&s,web_now())==403);
}
static void password_change_tests(const char *dir){
 web_security_t s,reboot;char path[512],original[512],token[65],csrf[65];unsigned char hash[32];
 snprintf(path,sizeof(path),"%s/change-admin",dir);assert(!web_security_init(&s,path));
 assert(!web_security_local(&s,1,web_now()));assert(!web_security_begin(&s,1,s.code,"old-password-123",web_now()));assert(finish(&s)==200);
 strcpy(token,s.token);strcpy(csrf,s.csrf);memcpy(hash,s.hash,32);
 assert(web_security_change(&s,"old-password-123","next-password-123","mismatch",web_now())==400);assert(!s.worker);
 assert(!web_security_change(&s,"wrong-password-123","next-password-123","next-password-123",web_now()));assert(finish(&s)==403);assert(!memcmp(hash,s.hash,32));
 strcpy(original,s.path);snprintf(s.path,sizeof(s.path),"%s/absent/admin",dir);
 assert(!web_security_change(&s,"old-password-123","next-password-123","next-password-123",web_now()));assert(finish(&s)==503);assert(!s.save_failed&&!memcmp(hash,s.hash,32));assert(web_security_authorized(&s,token,csrf,1,web_now()));strcpy(s.path,original);
 assert(!web_security_local(&s,2,web_now()));assert(web_security_authorized(&s,token,csrf,1,web_now()));assert(!web_security_local(&s,0,web_now()));assert(web_security_authorized(&s,token,csrf,1,web_now()));
 assert(!web_security_change(&s,"old-password-123","next-password-123","next-password-123",web_now()));assert(web_security_change(&s,"old-password-123","next-password-123","next-password-123",web_now())==423);assert(finish(&s)==200);assert(!s.token[0]&&!s.csrf[0]);assert(!web_security_authorized(&s,token,csrf,1,web_now()));
 assert(!web_security_init(&reboot,path));assert(!web_security_begin(&reboot,0,"","old-password-123",web_now()));assert(finish(&reboot)==403);assert(!web_security_begin(&reboot,0,"","next-password-123",web_now()));assert(finish(&reboot)==200);
 /* Rename succeeded but directory fsync failed: never claim durable success. */
 assert(!web_security_change(&reboot,"next-password-123","third-password-123","third-password-123",web_now()));
 setenv("WEB_COMMIT_DIRSYNC_FAIL","1",1);assert(finish(&reboot)==503);unsetenv("WEB_COMMIT_DIRSYNC_FAIL");assert(reboot.save_failed&&!reboot.token[0]);
 web_security_cancel(&s);web_security_cancel(&reboot);
 assert(!web_security_init(&reboot,path));assert(!web_security_begin(&reboot,0,"","third-password-123",web_now()));assert(finish(&reboot)==200);
 memcpy(hash,reboot.hash,32);strcpy(token,reboot.token);strcpy(csrf,reboot.csrf);
 assert(!web_security_change(&reboot,"third-password-123","stale-password-123","stale-password-123",web_now()));
 assert(!web_security_local(&reboot,2,web_now()));assert(!reboot.worker);assert(web_security_poll(&reboot,web_now())==403);
 assert(!memcmp(hash,reboot.hash,32)&&web_security_authorized(&reboot,token,csrf,1,web_now()));
 web_security_cancel(&reboot);
}
int main(void){char worker[512],*slash;ssize_t z=readlink("/proc/self/exe",worker,sizeof(worker)-1);assert(z>0);worker[z]=0;slash=strrchr(worker,'/');assert(slash);strcpy(slash+1,"4vrs-kdf");web_security_worker_path(worker);web_frame_t a,b;web_fields_t f;web_http_t h;web_security_t s,reboot;web_certificate_t cert;char text[WEB_HTTP_MAX+1],dir[]="/tmp/web-core-XXXXXX",path[256],code[13],token[65],csrf[65],first[8192],second[8192];size_t i,n,m;uint32_t now;
 assert(mkdtemp(dir));password_change_tests(dir);assert(!web_frame_make(&a,"op=hello\n"));memset(&b,0,sizeof(b));for(i=0;i<a.total;++i)assert(web_frame_feed(&b,a.bytes+i,1)==(i+1==a.total));assert(web_frame_feed(&b,"x",1)<0);
 memset(&b,0,sizeof(b));a.bytes[4]=1;assert(web_frame_feed(&b,a.bytes,8)<0);assert(web_fields_parse(&f,"op=hello\nop=x\n",14)<0);assert(web_fields_parse(&f,"op=%0a\n",7)<0);assert(web_fields_parse(&f,"op=x",4)<0);
 strcpy(text,"GET /api/hello HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");assert(web_http_parse(text,strlen(text),"127.0.0.1",1,&h)==1);assert(web_http_parse(text,strlen(text),"127.0.0.2",1,&h)==-400);
 strcpy(text,"POST /api/login HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n");assert(web_http_parse(text,strlen(text),"127.0.0.1",1,&h)==-403);
 strcpy(text,"POST /api/login HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: https://evil\r\nContent-Length: 0\r\n\r\n");assert(web_http_parse(text,strlen(text),"127.0.0.1",1,&h)==-403);
 snprintf(path,sizeof(path),"%s/admin",dir);assert(!web_security_init(&s,path));now=web_now();no_entropy=1;assert(web_security_local(&s,1,now)<0);no_entropy=0;assert(!web_security_local(&s,1,now));strcpy(code,s.code);
 for(i=0;i<5;++i)assert(web_security_begin(&s,1,"bad-code","test-password-123",now)==403);assert(!s.code[0]);assert(!web_security_local(&s,1,0xfffffff0U));web_security_tick(&s,0xfffffff0U+WEB_CODE_MS);assert(!s.code[0]);
 assert(!web_security_local(&s,1,web_now()));strcpy(code,s.code);assert(!web_security_begin(&s,1,code,"test-password-123",web_now()));assert(web_security_begin(&s,1,code,"test-password-123",web_now())==423);assert(finish(&s)==200);assert(s.administrator&&!s.code[0]);strcpy(token,s.token);strcpy(csrf,s.csrf);assert(web_security_begin(&s,1,code,"test-password-123",web_now())==403);
 assert(web_security_authorized(&s,token,csrf,1,web_now()));assert(!web_security_authorized(&s,token,"",1,web_now()));web_security_tick(&s,s.touched+WEB_SESSION_IDLE_MS);assert(!s.token[0]);assert(!web_security_init(&reboot,path));assert(reboot.administrator&&!reboot.code[0]&&!reboot.token[0]);
 assert(!web_security_begin(&reboot,0,"","test-password-123",web_now()));assert(finish(&reboot)==200);assert(!web_security_local(&reboot,1,web_now())&&!reboot.code[0]);assert(!web_security_local(&reboot,2,web_now()));assert(reboot.recovery&&reboot.token[0]);assert(!web_security_local(&reboot,0,web_now())&&!reboot.code[0]);
 for(i=0;i<4;++i){assert(!web_security_begin(&reboot,0,"","wrong-password-123",web_now()));assert(finish(&reboot)==403);}assert(web_security_begin(&reboot,0,"","test-password-123",web_now())==429);
 snprintf(path,sizeof(path),"%s/absent/admin",dir);assert(!web_security_init(&s,path));assert(!web_security_local(&s,1,web_now()));strcpy(code,s.code);assert(!web_security_begin(&s,1,code,"test-password-123",web_now()));assert(finish(&s)==503);assert(!s.save_failed&&!s.token[0]&&!s.code[0]);
 assert(!web_security_local(&s,1,web_now()));assert(!web_security_begin(&s,1,s.code,"test-password-123",web_now()));web_security_cancel(&s);assert(!s.worker&&!s.token[0]&&!s.code[0]);
 bad_time=1;assert(web_certificate_open(&cert,dir,"127.0.0.1","-")!=0);bad_time=0;no_entropy=1;assert(web_certificate_open(&cert,dir,"127.0.0.1","-")!=0);web_certificate_close(&cert);no_entropy=0;
 assert(!web_certificate_open(&cert,dir,"127.0.0.1","-"));web_certificate_close(&cert);snprintf(path,sizeof(path),"%s/tls.current",dir);assert(!web_read_file(path,first,sizeof(first),&n));assert(!web_certificate_open(&cert,dir,"127.0.0.1","-"));web_certificate_close(&cert);assert(!web_read_file(path,second,sizeof(second),&m)&&n==m&&!memcmp(first,second,n));
 time_shift=174L*86400L;assert(!web_certificate_open(&cert,dir,"127.0.0.1","-"));assert(!web_certificate_due(&cert));web_certificate_close(&cert);assert(!web_read_file(path,second,sizeof(second),&m)&&strcmp(first,second));time_shift=0;
 assert(!web_certificate_open(&cert,dir,"127.0.0.2","-"));web_certificate_close(&cert);assert(!web_read_file(path,second,sizeof(second),&m)&&strcmp(first,second));assert(!strncmp(strstr(first,"-----BEGIN"),strstr(second,"-----BEGIN"),(size_t)(strstr(first,"-----BEGIN CERTIFICATE-----")-strstr(first,"-----BEGIN"))));
 {int fd;char staged[512];snprintf(staged,sizeof(staged),"%s.next",path);fd=open(staged,O_CREAT|O_WRONLY|O_EXCL,0600);assert(fd>=0);assert(write(fd,"partial",7)==7);close(fd);assert(!web_atomic_file(path,"recovered",9));assert(!web_read_file(path,first,sizeof(first),&n)&&n==9&&!strcmp(first,"recovered"));assert(!symlink(path,staged));assert(web_atomic_file(path,"refused",7)<0);unlink(staged);}
 web_security_cancel(&s);web_security_cancel(&reboot);late_commit_tests(dir);puts("web core: framing, HTTP, enrollment, session, recovery, faults, certificate PASS");return 0;}
