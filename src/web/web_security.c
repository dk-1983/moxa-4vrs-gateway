#define _GNU_SOURCE
#include "web/exec_fds.h"
#include "web/kdf_worker.h"
#include <sys/socket.h>
#include "web/web_security.h"
#include "mbedtls/pkcs5.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
static char worker_path[512]="/var/hda/4vrs/bin/4vrs-kdf";
static int retired[8];
void web_security_worker_path(const char *p){if(strlen(p)<sizeof(worker_path))strcpy(worker_path,p);}
static void reap(void){unsigned int i;for(i=0;i<8;i++)if(retired[i]&&waitpid(retired[i],NULL,WNOHANG)!=0)retired[i]=0;}
static void stop_worker(web_security_t *s){unsigned int i;if(s->commit_pending){s->save_failed=1;s->commit_pending=0;web_clear(s->token,sizeof(s->token));web_clear(s->csrf,sizeof(s->csrf));}reap();if(s->worker>0){kill(s->worker,SIGKILL);for(i=0;i<8;i++)if(!retired[i]){retired[i]=s->worker;break;}}if(s->worker_fd>=0)close(s->worker_fd);s->worker=0;s->worker_fd=-1;}
static int launch(web_security_t*s,const char*password,const char*next,int change,uint32_t now,int enroll){
 unsigned char b[KDF_WIRE_SIZE];int fds[2],pid,childfd;unsigned int i;ssize_t n;
 reap();for(i=0;i<8&&retired[i];i++);if(i==8)return 503;
 memset(b,0,sizeof(b));memcpy(b,"KDF1",4);b[4]=(unsigned char)change;b[5]=(unsigned char)strlen(password);b[6]=next?(unsigned char)strlen(next):0;
 memcpy(b+8,password,b[5]);if(next)memcpy(b+136,next,b[6]);memcpy(b+264,enroll==1?s->pending_salt:s->salt,16);memcpy(b+280,s->pending_salt,16);memcpy(b+296,s->hash,32);
 if(socketpair(AF_UNIX,SOCK_STREAM,0,fds)){web_clear(b,sizeof(b));return 503;}
 childfd=fcntl(fds[1],F_DUPFD,10);close(fds[1]);if(childfd<0){close(fds[0]);web_clear(b,sizeof(b));return 503;}
 pid=fork();if(!pid){if(dup2(childfd,3)<0||web_exec_close_from(4))_exit(127);close(0);close(1);execl(worker_path,worker_path,(char*)NULL);_exit(127);}
 close(childfd);if(pid<0){close(fds[0]);web_clear(b,sizeof(b));return 503;}
 s->worker=pid;s->worker_fd=fds[0];s->worker_enroll=enroll;s->worker_at=now;
 if(web_nonblock(fds[0])){web_clear(b,sizeof(b));stop_worker(s);return 503;}
 n=send(fds[0],b,sizeof(b),MSG_NOSIGNAL);web_clear(b,sizeof(b));if(n!=KDF_WIRE_SIZE){stop_worker(s);return 503;}shutdown(fds[0],SHUT_WR);return 0;
}
static int launch_commit(web_security_t*s,const unsigned char*hash){unsigned char b[KDF_COMMIT_SIZE];int f[2],x,pid;ssize_t n;
 if(strlen(s->path)>=512)return -1;memset(b,0,sizeof(b));memcpy(b,"KDC1",4);strcpy((char*)b+4,s->path);memcpy(b+516,s->pending_salt,16);memcpy(b+532,hash,32);
 if(socketpair(AF_UNIX,SOCK_STREAM,0,f)){web_clear(b,sizeof(b));return -1;}x=fcntl(f[1],F_DUPFD,10);close(f[1]);if(x<0){close(f[0]);web_clear(b,sizeof(b));return -1;}
 pid=fork();if(!pid){if(dup2(x,3)<0||web_exec_close_from(4))_exit(127);close(0);close(1);execl(worker_path,worker_path,"commit",(char*)NULL);_exit(127);}
 close(x);if(pid<0){close(f[0]);web_clear(b,sizeof(b));return -1;}s->worker=pid;s->worker_fd=f[0];s->commit_pending=1;
 if(web_nonblock(f[0])){web_clear(b,sizeof(b));stop_worker(s);return -1;}n=send(f[0],b,sizeof(b),MSG_NOSIGNAL);web_clear(b,sizeof(b));if(n!=KDF_COMMIT_SIZE){stop_worker(s);return -1;}shutdown(f[0],SHUT_WR);return 0;
}
int web_security_change(web_security_t *s,const char *current,const char *password,const char *repeat,uint32_t now){
 size_t n=strlen(password),old=strlen(current);
 web_security_tick(s,now);
 if(s->worker||s->code[0])return 423;
 if(!s->administrator||s->save_failed)return 403;
 if(n<12||n>128||old<12||old>128||strcmp(password,repeat))return 400;
 if(now-s->rate_at>=60000U){s->rate_at=now;s->login_attempts=0;}
 if(s->login_attempts>=5)return 429;++s->login_attempts;
 if(web_random(s->pending_salt,16))return 503;
 return launch(s,current,password,1,now,2);
}
void web_security_cancel_pending(web_security_t *s){stop_worker(s);web_clear(s->code,sizeof(s->code));s->recovery=0;}
void web_security_cancel(web_security_t *s){web_security_cancel_pending(s);web_clear(s->token,sizeof(s->token));web_clear(s->csrf,sizeof(s->csrf));s->recovery=0;}
int web_security_init(web_security_t *s,const char *path){char b[256];size_t n;web_fields_t f;int r;memset(s,0,sizeof(*s));s->worker_fd=-1;if(strlen(path)>=sizeof(s->path))return -1;strcpy(s->path,path);r=web_read_file(path,b,sizeof(b),&n);if(r==1)return 0;if(r||web_fields_parse(&f,b,n)||f.count!=2||web_unhex(web_field(&f,"salt"),s->salt,16)||web_unhex(web_field(&f,"hash"),s->hash,32)){s->save_failed=1;return -1;}s->administrator=1;return 0;}
int web_security_local(web_security_t *s,unsigned int action,uint32_t now){unsigned char random[6];if(action==0){web_security_cancel_pending(s);return 0;}if(action==1&&s->administrator&&!s->save_failed)return 0;if(s->code[0])return 0;web_security_cancel_pending(s);if(web_random(random,sizeof(random)))return -1;web_hex(random,sizeof(random),s->code);web_clear(random,sizeof(random));s->code_at=now;s->attempts=0;s->recovery=action==2;return 0;}
void web_security_tick(web_security_t *s,uint32_t now){reap();if(s->code[0]&&now-s->code_at>=WEB_CODE_MS){stop_worker(s);web_clear(s->code,sizeof(s->code));s->recovery=0;}if(s->token[0]&&(now-s->session_at>=WEB_SESSION_MAX_MS||now-s->touched>=WEB_SESSION_IDLE_MS)){web_clear(s->token,sizeof(s->token));web_clear(s->csrf,sizeof(s->csrf));}}
int web_security_begin(web_security_t *s,int enroll,const char *code,const char *password,uint32_t now){size_t n=strlen(password);web_security_tick(s,now);if(s->worker)return 423;if(n<12||n>128)return 400;
 if(enroll){if(!s->code[0]||s->attempts>=5||(!s->recovery&&s->administrator))return 403;++s->attempts;if(strlen(code)!=12||!web_equal(code,s->code,12)){if(s->attempts==5)web_clear(s->code,sizeof(s->code));return 403;}}
 else {if(!s->administrator||s->save_failed)return 403;if(now-s->rate_at>=60000U){s->rate_at=now;s->login_attempts=0;}if(s->login_attempts>=5)return 429;++s->login_attempts;}
 if(enroll&&web_random(s->pending_salt,16))return 503;
 return launch(s,password,NULL,0,now,enroll);}

int web_security_poll(web_security_t *s,uint32_t now){unsigned char hash[32],random[32];int status,r;ssize_t n;if(!s->worker)return 403;if(now-s->worker_at>=60000U){stop_worker(s);return 503;}r=waitpid(s->worker,&status,WNOHANG);if(!r)return 0;if(r<0||!WIFEXITED(status)||WEXITSTATUS(status)){int denied=r>0&&WIFEXITED(status)&&WEXITSTATUS(status)==2;if(s->commit_pending)web_clear(s->code,sizeof(s->code));if(s->commit_pending&&r>0&&WIFEXITED(status)&&WEXITSTATUS(status)==4)s->commit_pending=0;s->worker=0;stop_worker(s);if(s->save_failed){web_clear(s->token,sizeof(s->token));web_clear(s->csrf,sizeof(s->csrf));}return denied?403:503;}s->worker=0;n=read(s->worker_fd,hash,sizeof(hash));close(s->worker_fd);s->worker_fd=-1;if(n!=32){web_clear(hash,sizeof(hash));stop_worker(s);return 503;}
 if(s->worker_enroll){int replacement=s->administrator;if(s->worker_enroll==1&&(!s->code[0]||now-s->code_at>=WEB_CODE_MS)){web_clear(hash,sizeof(hash));stop_worker(s);web_clear(s->code,sizeof(s->code));s->recovery=0;return 403;}if(!s->commit_pending){r=launch_commit(s,hash);web_clear(hash,sizeof(hash));return r?503:0;}s->commit_pending=0;memcpy(s->salt,s->pending_salt,16);memcpy(s->hash,hash,32);s->administrator=1;s->recovery=0;s->save_failed=0;web_clear(s->code,sizeof(s->code));if(replacement){web_clear(hash,sizeof(hash));web_clear(s->token,sizeof(s->token));web_clear(s->csrf,sizeof(s->csrf));return 200;}}
 else if(!web_equal(hash,s->hash,32)){web_clear(hash,sizeof(hash));return 403;}
 web_clear(hash,sizeof(hash));web_clear(s->token,sizeof(s->token));if(web_random(random,32))return 503;web_hex(random,32,s->token);if(web_random(random,32)){web_clear(s->token,sizeof(s->token));return 503;}web_hex(random,32,s->csrf);web_clear(random,sizeof(random));s->session_at=s->touched=now;return 200;}
int web_security_authorized(web_security_t *s,const char *token,const char *csrf,int write,uint32_t now){web_security_tick(s,now);if(s->save_failed||!s->administrator||strlen(token)!=64||!s->token[0]||!web_equal(token,s->token,64))return 0;if(write&&(strlen(csrf)!=64||!web_equal(csrf,s->csrf,64)))return 0;s->touched=now;return 1;}
