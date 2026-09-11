#define _GNU_SOURCE
/* Ubuntu host worker. No target code changes; only the final schema-1 record.
 * Parent validates topology/mount policy; passed fds pin the verified objects.
 * All entropy and secret serialization stay in this short-lived native worker. */
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/random.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"

static volatile sig_atomic_t stopped;
static void stop(int sig){stopped=sig;}
static int stage(const char *s){
#ifdef CF_FIXTURE
 const char *f=getenv("CF_FAIL"),*c=getenv("CF_CRASH");
 if(c&&!strcmp(c,s))_exit(77);
 if(f&&!strcmp(f,s))return -1;
#else
 (void)s;
#endif
 return stopped?-1:0;
}
static unsigned le16(const unsigned char*p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t le32(const unsigned char*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t be32(const unsigned char*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int hex(const char *s,unsigned char out[16]){
 unsigned i;char *end,b[3]={0};if(strlen(s)!=32)return -1;
 for(i=0;i<16;i++){unsigned long n;b[0]=s[i*2];b[1]=s[i*2+1];n=strtoul(b,&end,16);if(*end||end!=b+2)return -1;out[i]=(unsigned char)n;}return 0;
}
#ifdef CF_GENERIC_TARGET
/* New wizard only; legacy builds retain their original fixed-target contract. */
static int target_mac(const char *s){
 unsigned i,nonzero=0,first=0;
 if(strlen(s)!=17)return -1;
 for(i=0;i<17;i++){
  if(i%3==2){if(s[i]!=':')return -1;continue;}
  if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return -1;
  nonzero|=s[i]!='0';
  if(i<2)first=first*16U+(unsigned)(s[i]<='9'?s[i]-'0':s[i]-'a'+10);
 }
 return !nonzero||(first&1U)?-1:0;
}
#endif
static int metadata(int fd,int dir){struct stat s;if(fstat(fd,&s)||s.st_uid||s.st_gid)return -1;
 return dir?(!S_ISDIR(s.st_mode)||(s.st_mode&07777)!=0700):(!S_ISREG(s.st_mode)||(s.st_mode&07777)!=0600||s.st_nlink!=1);}
static int absent(int fd,const char *name){struct stat s;return fstatat(fd,name,&s,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT;}
static int media(int root,int part,int disk,uint64_t ds,uint64_t ps,const unsigned char uuid[16],int writing){
 unsigned char sb[128];struct stat r,p,d;uint64_t dn,pn;int ro;
 if(fstat(root,&r)||fstat(part,&p)||fstat(disk,&d)||!S_ISDIR(r.st_mode)||r.st_uid||r.st_gid||(r.st_mode&0022))return -1;
#ifndef CF_FIXTURE
 {struct statfs fs;if(!S_ISBLK(p.st_mode)||!S_ISBLK(d.st_mode)||p.st_rdev==d.st_rdev||r.st_dev!=p.st_rdev||r.st_ino!=2||fstatfs(root,&fs)||fs.f_type!=0xef53)return -1;}
 if(ioctl(part,BLKGETSIZE64,&pn)||ioctl(disk,BLKGETSIZE64,&dn)||dn!=ds||pn!=ps)return -1;
 if(ioctl(part,BLKROGET,&ro)||(writing&&ro)||ioctl(disk,BLKROGET,&ro)||(writing&&ro))return -1;
#else
 (void)dn;(void)pn;(void)ro;(void)writing;if(!S_ISREG(p.st_mode)||!S_ISREG(d.st_mode)||(uint64_t)p.st_size!=ps||(uint64_t)d.st_size!=ds)return -1;
#endif
 if(pread(part,sb,sizeof(sb),1024)!=sizeof(sb)||le16(sb+56)!=0xef53||memcmp(sb+104,uuid,16))return -1;
 if(!(le32(sb+92)&4)||(le32(sb+96)&~6U)||(le32(sb+100)&~7U)||le16(sb+58)&2)return -1;
 if(le32(sb+24)>2||!le32(sb+4)||(uint64_t)le32(sb+4)*(1024U<<le32(sb+24))>ps)return -1;
 return 0;
}
static int verify(int dir,const unsigned char binding[64],unsigned char rec[192]){
 int fd=-1,result=-1;struct stat s;unsigned char digest[32];
 if(!absent(dir,"pending")||!absent(dir,"attempt")||!absent(dir,"witness-next"))return -1;
 fd=openat(dir,"owner.lock",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -1;
 if(metadata(fd,0)||fstat(fd,&s)||s.st_size){close(fd);return -1;}
 if(close(fd))return -1;
 fd=openat(dir,"state",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC|O_NOATIME);if(fd<0)return -1;
 if(metadata(fd,0)||fstat(fd,&s)||(s.st_size!=160&&s.st_size!=192)||pread(fd,rec,(size_t)s.st_size,0)!=s.st_size)goto done;
 if(!be32(rec+8)||memcmp(rec+16,binding,64))goto done;
 if(s.st_size==160){if(memcmp(rec,"4VRSNV01",8)||mbedtls_sha256(rec,128,digest,0)||memcmp(rec+128,digest,32))goto done;}
 else {
  if(!be32(rec+12)||mbedtls_sha256(rec,160,digest,0)||memcmp(rec+160,digest,32))goto done;
  if(!memcmp(rec,"4VRSNV02",8)){if(be32(rec+12)>8||memcmp(rec+128,"production-v1",13))goto done;}
  else if(!memcmp(rec,"4VRSNV03",8)){
   unsigned char witness[192];int wf,match;
   if(memcmp(rec+128,"production-autonomous-v1",24))goto done;
   wf=openat(dir,"witness",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC|O_NOATIME);
   if(wf<0)goto done;
   match=!metadata(wf,0)&&!fstat(wf,&s)&&s.st_size==192&&pread(wf,witness,192,0)==192&&!memcmp(witness,rec,192);
   if(close(wf))match=0;mbedtls_platform_zeroize(witness,sizeof(witness));
   if(!match)goto done;
  }else goto done;
 }
 result=0;
done:if(close(fd))result=-1;mbedtls_platform_zeroize(digest,32);return result;
}
int main(int argc,char **argv){
 int root,part,disk,dir=-1,lock=-1,fd=-1,r=1,writing,locked=0;uint64_t ds,ps;char *end,material[80];
 unsigned char uuid[16],binding[64]={0},rec[192]={0};struct rlimit zero={0,0};struct sigaction sa;struct flock fl;
 if(argc!=10||geteuid()||hex(argv[6],uuid))return 2;
#ifdef CF_GENERIC_TARGET
 if(target_mac(argv[5])||strcmp(argv[9],"confirmed-target"))return 2;
#else
 if(strcmp(argv[5],"00:90:e8:1f:4c:f1")||strcmp(argv[9],"confirmed-moxa1"))return 2;
#endif
 writing=!strcmp(argv[1],"prepare");if(!writing&&strcmp(argv[1],"verify"))return 2;
 root=atoi(argv[2]);part=atoi(argv[3]);disk=atoi(argv[4]);if(root<3||part<3||disk<3)return 2;
 ds=strtoull(argv[7],&end,10);if(*end||!ds)return 2;ps=strtoull(argv[8],&end,10);if(*end||!ps||ps>=ds)return 2;
 if(setrlimit(RLIMIT_CORE,&zero))return 1;
 memset(&sa,0,sizeof(sa));sa.sa_handler=stop;sigemptyset(&sa.sa_mask);sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);signal(SIGPIPE,SIG_IGN);
 snprintf(material,sizeof(material),"4vrs-nv-binding-v1:%s",argv[5]);
 if(mbedtls_sha256((unsigned char*)material,strlen(material),binding,0))goto done;memcpy(binding+32,uuid,16);
 if(stage("precheck")||media(root,part,disk,ds,ps,uuid,writing))goto done;
 if(mlock(rec,sizeof(rec)))goto done;locked=1;
 if(!writing){dir=openat(root,"4vrs-rng",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0||metadata(dir,1)||verify(dir,binding,rec))goto done;
  printf("state=valid generation=%u\n",be32(rec+8));r=0;goto done;}
 /* Refuse even an empty previous directory. No cleanup/reseed/retry command. */
 if(!absent(root,"4vrs-rng")||stage("before-mkdir")||mkdirat(root,"4vrs-rng",0700)||stage("mkdir")||fsync(root)||stage("parent-fsync"))goto done;
 dir=openat(root,"4vrs-rng",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0||metadata(dir,1))goto done;
 lock=openat(dir,"owner.lock",O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(lock<0||metadata(lock,0))goto done;
 memset(&fl,0,sizeof(fl));fl.l_type=F_WRLCK;fl.l_whence=SEEK_SET;if(fcntl(lock,F_SETLK,&fl)||fsync(lock)||fsync(dir)||stage("lock-fsync"))goto done;
 if(media(root,part,disk,ds,ps,uuid,1)||!absent(dir,"state")||!absent(dir,"pending")||stage("random"))goto done;
 memcpy(rec,"4VRSNV01",8);rec[11]=1;memcpy(rec+16,binding,64);
 /* GRND_NONBLOCK fails with EAGAIN before kernel CSPRNG initialization.
  * No /dev fallback, entropy-count heuristic, retry or partial result. */
 if(getrandom(rec+96,32,GRND_NONBLOCK)!=32)goto done;
 if(stage("after-random")||mbedtls_sha256(rec,128,rec+128,0))goto done;
 fd=openat(dir,"pending",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0||metadata(fd,0)||stage("created")||stage("before-write")||write(fd,rec,160)!=160||stage("written"))goto done;
 if(stage("before-file-fsync")||fsync(fd)||stage("file-fsync"))goto done;
 if(close(fd)){fd=-1;goto done;}fd=-1;if(stage("closed"))goto done;
 if(stage("before-rename")||renameat2(dir,"pending",dir,"state",RENAME_NOREPLACE)||stage("renamed"))goto done;
 if(stage("before-directory-fsync")||fsync(dir)||stage("directory-fsync")||verify(dir,binding,rec)||stage("before-ack"))goto done;
 if(puts("state=prepared generation=1 policy=required")<0||fflush(stdout))goto done;r=0;
done:
 mbedtls_platform_zeroize(rec,sizeof(rec));mbedtls_platform_zeroize(binding,sizeof(binding));
 if(locked)munlock(rec,sizeof(rec));
 if(fd>=0)close(fd);if(lock>=0)close(lock);if(dir>=0)close(dir);
 if(r)fputs("CF operation not confirmed; inspect/verify before any further action; no automatic retry\n",stderr);
 return r;
}
