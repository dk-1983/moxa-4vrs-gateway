/* Production single-owner NV backend. Only broker/helper may link this module.
 * No fork, threads or runtime state outside the exec owner.
 * State and any crash leftovers are confidential. Never print their contents. */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include "web/rng_nv.h"
#ifndef O_NOATIME
#define O_NOATIME 01000000
#endif

#define RECORD 192
#define LEGACY_RECORD 160
#define SEED_OFFSET 96
#define DIGEST_OFFSET 128
#define REQUEST_LIMIT 1024
static int dirfd_ = -1, lockfd_ = -1, committed, ready, poisoned;
static unsigned long generation, requests;
static int production, schema, failure;
static unsigned long credits_used;
static pid_t owner;
static unsigned char binding[64];
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;

static int stage(const char *name)
{
    /* Events carry no state bytes. Fault injection exists only in host builds. */
    (void)name;
#ifdef WEB_HOST_TEST
    printf("stage=%s\n", name); fflush(stdout);
#endif
#ifdef WEB_HOST_TEST
    if (getenv("NV_CRASH") && !strcmp(getenv("NV_CRASH"), name)) _exit(77);
    if (getenv("NV_FAIL") && !strcmp(getenv("NV_FAIL"), name)) return -1;
#endif
    return 0;
}

static int metadata(int fd, int directory)
{
    struct stat s;
    if (fstat(fd, &s) || s.st_uid != geteuid()) return -1;
    if (directory) return S_ISDIR(s.st_mode) && (s.st_mode & 0777) == 0700 ? 0 : -1;
    return S_ISREG(s.st_mode) && (s.st_mode & 0777) == 0600 && s.st_nlink == 1 ? 0 : -1;
}

/* openat is unavailable on the target libc. Walk with open(O_NOFOLLOW)+fchdir
 * in this single-threaded executable; reject writable/untrusted ancestors. */
static int enter_directory(const char *path)
{
    char copy[PATH_MAX], *part, *save = NULL;
    struct stat s; int fd;
    if (path[0] != '/' || strlen(path) >= sizeof(copy)) return -1;
    strcpy(copy, path);
    if (chdir("/")) return -1;
    part = strtok_r(copy, "/", &save);
    while (part) {
        if (!strcmp(part, ".") || !strcmp(part, "..")) return -1;
        fd = open(part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (fd < 0) return -1;
        if (fstat(fd, &s) || !S_ISDIR(s.st_mode) ||
            (s.st_uid != 0 && s.st_uid != geteuid()) ||
#ifdef WEB_HOST_TEST
            ((s.st_mode & 0022) && !(s.st_mode & S_ISVTX)) ||
#else
            (s.st_mode & 0022) ||
#endif
            fchdir(fd)) { close(fd); return -1; }
        close(fd); part = strtok_r(NULL, "/", &save);
    }
    dirfd_ = open(".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    return dirfd_ < 0 ? -1 : metadata(dirfd_, 1);
}

static int cf_binding(const char *identity)
{
    struct statfs fs;
    unsigned char digest[32], super[128];
    char material[256]; int n;
    if (strlen(identity) < 1 || strlen(identity) > 80 || fstatfs(dirfd_, &fs)) return -1;
#ifndef WEB_HOST_TEST
    /* Fail closed on absent CF mount, wrong filesystem/device, read-only mount.
     * ext2/ext3 share magic; /proc/mounts must explicitly say ext3 and rw. */
    FILE *f; char line[512], dev[128], mount[128], type[32], opts[128], mac[18];
    struct ifreq ifr; int sock, raw;
    struct stat here, cf, block; int found = 0;
    if (geteuid() != 0 || fs.f_type != 0xef53 || fstat(dirfd_, &here) ||
        stat("/var/hda", &cf) || stat("/dev/hda1", &block) ||
        !S_ISBLK(block.st_mode) || here.st_dev != cf.st_dev || here.st_dev != block.st_rdev) return -1;
    f = fopen("/proc/mounts", "r"); if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%127s %127s %31s %127s", dev, mount, type, opts) == 4 &&
            !strcmp(dev, "/dev/hda1") && !strcmp(mount, "/var/hda") &&
            !strcmp(type, "ext3") && (!strcmp(opts, "rw") || !strncmp(opts, "rw,", 3))) found++;
    }
    fclose(f); if (found != 1) return -1;
    raw = open("/dev/hda1", O_RDONLY | O_NOFOLLOW); if (raw < 0) return -1;
    if (fstat(raw, &block) || !S_ISBLK(block.st_mode) || block.st_rdev != here.st_dev ||
        pread(raw, super, sizeof(super), 1024) != sizeof(super)) { close(raw); return -1; }
    if (close(raw)) return -1;
    memset(&ifr, 0, sizeof(ifr)); strcpy(ifr.ifr_name, "eth0");
    sock = socket(AF_INET, SOCK_DGRAM, 0); if (sock < 0) return -1;
    if (ioctl(sock, SIOCGIFHWADDR, &ifr)) { close(sock); return -1; }
    close(sock);
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
        (unsigned char)ifr.ifr_hwaddr.sa_data[0], (unsigned char)ifr.ifr_hwaddr.sa_data[1],
        (unsigned char)ifr.ifr_hwaddr.sa_data[2], (unsigned char)ifr.ifr_hwaddr.sa_data[3],
        (unsigned char)ifr.ifr_hwaddr.sa_data[4], (unsigned char)ifr.ifr_hwaddr.sa_data[5]);
    if (!strcmp(identity, "auto")) identity = mac;
    else if (strcmp(identity, mac)) return -1;
#else
    /* Host substitutes a disposable superblock header. Target reads actual CF.
     * 2.6.10 vendor ext3_statfs does NOT populate f_fsid. */
    memset(super, 0, sizeof(super)); super[56] = 0x53; super[57] = 0xef;
    if (sizeof(fs.f_fsid) > 16) return -1;
    memcpy(super + 104, &fs.f_fsid, sizeof(fs.f_fsid));
    if (getenv("NV_BAD_SUPER")) {
        if (!strcmp(getenv("NV_BAD_SUPER"), "magic")) super[56] = 0;
        else memset(super + 104, 0, 16);
    }
#endif
    if (super[56] != 0x53 || super[57] != 0xef) return -1;
    for (n = 0; n < 16 && super[104+n] == 0; n++) {}
    if (n == 16) return -1;
    n = snprintf(material, sizeof(material), "4vrs-nv-binding-v1:%s", identity);
    if (n < 0 || (size_t)n >= sizeof(material) || mbedtls_sha256((unsigned char *)material, (size_t)n, digest, 0)) return -1;
    memcpy(binding, digest, 32);
    /* UUID detects ordinary replacement, not a copied UUID/raw image. */
    memcpy(binding + 32, super + 104, 16);
    return 0;
}

static int lock_state(void)
{
    struct flock l;
    lockfd_ = open("owner.lock", O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (lockfd_ < 0 || metadata(lockfd_, 0) || fcntl(lockfd_, F_SETFD, FD_CLOEXEC)) return -1;
    memset(&l, 0, sizeof(l)); l.l_type = F_WRLCK; l.l_whence = SEEK_SET;
    if (fcntl(lockfd_, production?F_SETLK:F_SETLKW, &l)) return -1;
    return stage("locked");
}

static int recover_pending(void)
{
    int fd; struct stat s;
    if (lstat("pending", &s)) return errno == ENOENT ? 0 : -1;
    fd = open("pending", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (metadata(fd, 0)) { close(fd); return -1; }
    if (close(fd) || unlink("pending") || fsync(dirfd_)) return -1;
    /* No output was allowed before pending became durable state. Discard only
     * this uncommitted temporary, never promote it or use it as fallback. */
    return stage("pending-discarded");
}

static unsigned long get32(const unsigned char *p)
{ return ((unsigned long)p[0]<<24)|((unsigned long)p[1]<<16)|((unsigned long)p[2]<<8)|p[3]; }
static void put32(unsigned char *p, unsigned long x)
{ p[0]=(unsigned char)(x>>24); p[1]=(unsigned char)(x>>16); p[2]=(unsigned char)(x>>8); p[3]=(unsigned char)x; }

static int read_record(unsigned char *r)
{
    int fd, ret = -1; struct stat s; unsigned char hash[32];
    fd = open("state", O_RDONLY | O_NOFOLLOW | O_NOATIME);
    if (fd < 0) return -1;
    memset(r, 0, RECORD);
    if (!metadata(fd, 0) && !fstat(fd, &s) &&
        (s.st_size == LEGACY_RECORD || s.st_size == RECORD) &&
        read(fd, r, (size_t)s.st_size) == s.st_size &&
        !memcmp(r + 16, binding, 64)) {
        size_t digest = s.st_size == RECORD ? 160 : 128;
        if ((!memcmp(r,"4VRSNV01",8) && s.st_size==LEGACY_RECORD) ||
            (!memcmp(r,"4VRSNV02",8) && s.st_size==RECORD &&
             !memcmp(r+128,"production-v1",13) && get32(r+12)>=1 && get32(r+12)<=8) ||
            (!memcmp(r,"4VRSNV03",8) && s.st_size==RECORD &&
             !memcmp(r+128,"production-autonomous-v1",24) && get32(r+12)>=1)) {
            if (!mbedtls_sha256(r,digest,hash,0) && !memcmp(hash,r+digest,32)) {
                schema=s.st_size==RECORD?(!memcmp(r,"4VRSNV03",8)?3:2):1;
                credits_used=schema>=2?get32(r+12):0;
                ret=0;
            }
        }
    }
    if (close(fd)) ret = -1;
    mbedtls_platform_zeroize(hash, sizeof(hash));
    return ret;
}

static int nv_read(unsigned char *buf, size_t n)
{
    unsigned char r[RECORD]; int ret = -1;
    if (poisoned || n != 32 || ready || stage("read")) return -1;
    if (!read_record(r)) { memcpy(buf, r + SEED_OFFSET, n); generation = get32(r + 8); ret = (int)n; }
    if (ret < 0) mbedtls_platform_zeroize(buf, n);
    mbedtls_platform_zeroize(r, sizeof(r)); return ret;
}

/* A retained full-record witness is committed before the state replacement.
 * Matching state+witness with no staging file proves a recoverable successor.
 * Unknown/partial transactions are never retried automatically. No cleanup is
 * needed at shutdown; the witness is replaced only by the next transaction. */
static int witness_matches(const unsigned char *r)
{
    unsigned char w[RECORD]; struct stat st; int fd,ret=-1;
    fd=open("witness",O_RDONLY|O_NOFOLLOW|O_NOATIME);
    if(fd<0)return -1;
    if(!metadata(fd,0)&&!fstat(fd,&st)&&st.st_size==RECORD&&
       read(fd,w,RECORD)==RECORD&&!memcmp(w,r,RECORD))ret=0;
    if(close(fd))ret=-1;
    mbedtls_platform_zeroize(w,sizeof(w));return ret;
}
static int write_witness(const unsigned char *r)
{
    int fd=open("witness-next",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600),ret=-1;
    if(fd<0)return -1;
    if(metadata(fd,0)||stage("witness-created")||stage("before-witness-write")||
       write(fd,r,RECORD)!=RECORD||stage("witness-written")||
       stage("before-witness-fsync")||fsync(fd)||stage("witness-fsync"))goto done;
    if(stage("before-witness-close"))goto done;
    if(close(fd)){fd=-1;goto done;}fd=-1;
    if(stage("witness-closed"))goto done;
    if(stage("before-witness-rename")||rename("witness-next","witness")||
       stage("witness-renamed")||stage("before-witness-directory-fsync")||
       fsync(dirfd_)||stage("witness-directory-fsync"))goto done;
    ret=0;
done:if(fd>=0)close(fd);return ret;
}

/* Recovery burns any authenticated-by-checksum, bound reservation too. This
 * is corruption detection, not protection against an administrator cloning CF. */
static void service_highwater(const char *name)
{
    unsigned char r[RECORD],hash[32];struct stat st;int fd;
    fd=open(name,O_RDONLY|O_NOFOLLOW|O_NOATIME);if(fd<0)return;
    if(!metadata(fd,0)&&!fstat(fd,&st)&&st.st_size==RECORD&&read(fd,r,RECORD)==RECORD&&
       !memcmp(r,"4VRSNV03",8)&&!memcmp(r+16,binding,64)&&
       !memcmp(r+128,"production-autonomous-v1",24)&&
       !mbedtls_sha256(r,160,hash,0)&&!memcmp(hash,r+160,32)){
        if(get32(r+8)>generation)generation=get32(r+8);
        if(get32(r+12)>credits_used)credits_used=get32(r+12);
    }
    close(fd);mbedtls_platform_zeroize(r,sizeof(r));mbedtls_platform_zeroize(hash,sizeof(hash));
}

static int persist(const unsigned char *seed, int provision)
{
    unsigned char r[RECORD]; int fd = -1, ret = -1, masked=0;
    sigset_t deferred,previous;
    size_t length=production?RECORD:LEGACY_RECORD, digest=production?160:128;
    struct stat s;
    memset(r, 0, sizeof(r));
    if (poisoned) goto done;
    /* SIGTERM/INT request orderly stop, never EINTR half a transaction.
     * Pending signals are delivered after the durable outcome is known.
     * Actual I/O errors still poison the owner; no write is retried. */
    sigemptyset(&deferred);sigaddset(&deferred,SIGTERM);sigaddset(&deferred,SIGINT);
    if(sigprocmask(SIG_BLOCK,&deferred,&previous))goto done;
    masked=1;

    if (generation >= 0xffffffffUL || credits_used >= 0xffffffffUL) { failure=8; goto done; }
    /* Caller has recovered uncommitted pending under the exclusive lock. */
    if (lstat("pending", &s) == 0 || errno != ENOENT) goto done;
    if (provision && (lstat("state", &s) == 0 || errno != ENOENT)) goto done;
    memcpy(r, production?"4VRSNV03":"4VRSNV01", 8); put32(r + 8, generation + 1);
    if(production){put32(r+12,credits_used+1);memcpy(r+128,"production-autonomous-v1",24);}
    memcpy(r + 16, binding, 64); memcpy(r + SEED_OFFSET, seed, 32);
    if (mbedtls_sha256(r, digest, r + digest, 0)) goto done;
    if(production && write_witness(r))goto done;
    fd = open("pending", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0 || metadata(fd, 0) || stage("created")) goto done;
    if (stage("before-write") || write(fd, r, length) != (ssize_t)length || stage("written")) goto done;
    if (stage("before-file-fsync") || fsync(fd) || stage("file-fsync")) goto done;
    if (stage("before-close")) goto done;
    if (close(fd)) { fd = -1; goto done; } fd = -1;
    if (stage("closed")) goto done;
    if (stage("before-rename") || rename("pending", "state") || stage("renamed")) goto done;
    if (stage("before-directory-fsync") || fsync(dirfd_) || stage("directory-fsync")) goto done;
    generation++; if(production){credits_used++;schema=3;} committed = 1; ret = 0;
done:
    if(ret && production && failure!=7){poisoned=1;failure=8;}
    if (fd >= 0) close(fd);
    mbedtls_platform_zeroize(r, sizeof(r));
    if(masked&&sigprocmask(SIG_SETMASK,&previous,NULL)){poisoned=1;failure=8;ret=-1;}
    return ret;
}

static int nv_write(unsigned char *buf, size_t n)
{ return poisoned || ready || n != 32 || persist(buf, 0) ? -1 : (int)n; }

static int entropy_once(void *ctx, unsigned char *out, size_t n)
{
    if (ready || poisoned) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    return mbedtls_entropy_func(ctx, out, n);
}

int rng_nv_random(unsigned char *out, size_t n)
{
    int ret = -1;
    if (ready && !poisoned && owner == getpid() && committed && requests < REQUEST_LIMIT && n <= 1024) {
        ret = mbedtls_ctr_drbg_random(&drbg, out, n);
        if (!ret) requests++;
    }
    if (ret) { mbedtls_platform_zeroize(out, n); ready = 0; }
    return ret;
}


void rng_nv_close(void)
{
    ready=0; mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy);
    if(lockfd_>=0)close(lockfd_); if(dirfd_>=0)close(dirfd_);
    lockfd_=dirfd_=-1; committed=0;
}
int rng_nv_open(const char *path)
{
    const char *id="auto";
    struct stat missing;
#ifdef WEB_HOST_TEST
    id="fixture-device-A";
#else
    if(strncmp(path,"/var/hda/",9))return -1;
#endif
    mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&drbg);
    if(lstat(path,&missing)&&errno==ENOENT){
#ifndef WEB_HOST_TEST
        struct stat cf,parent,block;
        if(stat("/var/hda",&cf)||stat("/var",&parent)||stat("/dev/hda1",&block)||cf.st_dev==parent.st_dev||cf.st_dev!=block.st_rdev)return 6;
#endif
        return 3;
    }
    if(enter_directory(path)||cf_binding(id))return -1;
    return 0;
}
static int exists(const char *name)
{ struct stat s; if(!lstat(name,&s))return 1; return errno==ENOENT?0:1; }
static int marker(const char *name,const char *value)
{
    char b[32];struct stat s;size_t n=strlen(value);int fd=open(name,O_RDONLY|O_NOFOLLOW|O_NOATIME),r=-1;
    if(fd<0)return -1;
    if(!metadata(fd,0)&&!fstat(fd,&s)&&s.st_size==(off_t)n&&read(fd,b,n)==(ssize_t)n&&!memcmp(b,value,n))r=0;
    if(close(fd))r=-1;return r;
}
static int active_owner(void)
{
    struct flock l;int active,fd=open("owner.lock",O_RDONLY|O_NOFOLLOW|O_NOATIME);
    if(fd<0)return 0;
    memset(&l,0,sizeof(l));l.l_type=F_WRLCK;l.l_whence=SEEK_SET;
    active=!metadata(fd,0)&&!fcntl(fd,F_GETLK,&l)&&l.l_type!=F_UNLCK;
    close(fd);return active;
}
/* Routine startup and shutdown never grant permissions or mutate markers. */
int rng_nv_finish(void){return poisoned||failure?-1:0;}
int rng_nv_service_finish(void){return rng_nv_finish();}
int rng_nv_status(void)
{
    unsigned char r[RECORD]; struct stat st; int result;
    if(lstat("state",&st))return errno==ENOENT?(exists("production-policy")?8:3):4;
    result=read_record(r)?4:0;
    if(!result){generation=get32(r+8);production=schema>=2||exists("production-policy");}
    if(!result&&production){
        if(schema!=3)result=8;
        else if(!active_owner()&&(exists("attempt")||exists("pending")||
                exists("witness-next")||witness_matches(r)))result=8;
    }
    mbedtls_platform_zeroize(r,sizeof(r));return result;
}
unsigned long rng_nv_generation(void){return generation;}
int rng_nv_provision(const unsigned char *seed)
{if(exists("production-policy")||schema>=2)return -1;generation=0;return lock_state()||recover_pending()||persist(seed,1)?-1:0;}
int rng_nv_start(void)
{
    const unsigned char perso[]="4vrs-product-rng-v1";
    if(poisoned)return -1;
    if(production){
        unsigned char current[RECORD]; int invalid;
        if(lockfd_<0&&lock_state()){failure=10;return -1;}
        invalid=read_record(current)||schema!=3||rng_nv_policy()||
            exists("attempt")||exists("pending")||exists("witness-next")||
            witness_matches(current);
        mbedtls_platform_zeroize(current,sizeof(current));
        if(invalid){failure=8;return -1;}
    }else if((lockfd_<0&&lock_state())||recover_pending())return -1;
    if(mbedtls_platform_set_nv_seed(nv_read,nv_write))return -1;
    if(mbedtls_ctr_drbg_seed(&drbg,entropy_once,&entropy,perso,sizeof(perso)-1)||!committed)return -1;
    ready=1;owner=getpid();requests=0;
    mbedtls_ctr_drbg_set_reseed_interval(&drbg,REQUEST_LIMIT);
    return 0; /* exclusive owner lock retained for entire broker lifetime */
}
int rng_nv_policy(void)
{
    if(schema>=2||exists("production-policy")){
        production=1;
        return schema==3&&!exists("trial-policy")?marker("production-policy","production-autonomous-v1\n"):-1;
    }
    production=0;return marker("trial-policy","trial-v1\n");
}
int rng_nv_production(void){return production;}
int rng_nv_schema(void){return schema;}
int rng_nv_error(void){return failure?failure:(production?8:4);}
unsigned long rng_nv_used(void){return credits_used;}
/* Explicit fresh-entropy activation/recovery. No clock assertion or quota reset.
 * Clean migration never discards old ambiguous transaction evidence. Recovery
 * is a distinct operator action. Known generations remain strictly increasing. */
int rng_nv_service(const unsigned char *fresh,int mode)
{
    unsigned char old[RECORD],material[96],seed[32];int valid,fd=-1,ret=-1;
    const char policy[]="production-autonomous-v1\n";
    if(lock_state())return -1;
    valid=!read_record(old);
    if(mode!=3){
        if((!valid&&exists("state"))||(valid&&schema==3)||exists("attempt")||
           exists("pending")||exists("witness-next")||exists("witness"))goto done;
    }
    generation=valid?get32(old+8):0;
    credits_used=valid&&schema>=2?get32(old+12):0;
    if(mode==3){service_highwater("witness");service_highwater("witness-next");service_highwater("pending");}
    if(generation>=0xffffffffUL||credits_used>=0xffffffffUL)goto done;
    memset(material,0,sizeof(material));memcpy(material,"4vrs-autonomous-service-v1",26);
    if(valid)memcpy(material+32,old+SEED_OFFSET,32);
    memcpy(material+64,fresh,32);
    if(mbedtls_sha256(material,sizeof(material),seed,0))goto done;
    production=1;failure=0;poisoned=0;
    /* Revoke old policy before altering records. Old helpers reject schema 3. */
    if(exists("trial-policy")&&(unlink("trial-policy")||fsync(dirfd_)))goto done;
    if(exists("production-policy")&&(unlink("production-policy")||fsync(dirfd_)))goto done;
    fd=open("production-policy",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0||write(fd,policy,sizeof(policy)-1)!=(ssize_t)(sizeof(policy)-1)||fsync(fd))goto done;
    if(close(fd)){fd=-1;goto done;}fd=-1;if(fsync(dirfd_))goto done;
    if(stage("service-policy"))goto done;
    /* Only explicit fresh service can remove uncertain evidence. */
    if(mode==3){
        const char *names[]={"attempt","pending","witness-next"};unsigned int i;
        for(i=0;i<sizeof(names)/sizeof(names[0]);i++)
            if(exists(names[i])&&(unlink(names[i])||fsync(dirfd_)))goto done;
    }
    if(exists("start-permit")&&(unlink("start-permit")||fsync(dirfd_)))goto done;
    if(persist(seed,0))goto done;
    ret=0;
done:
    if(fd>=0)close(fd);
    mbedtls_platform_zeroize(old,sizeof(old));mbedtls_platform_zeroize(material,sizeof(material));mbedtls_platform_zeroize(seed,sizeof(seed));
    if(ret){poisoned=1;failure=8;}return ret;
}
int rng_nv_rotate(void)
{
    ready=0;committed=0;
    mbedtls_ctr_drbg_free(&drbg);mbedtls_entropy_free(&entropy);
    mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&drbg);
    return rng_nv_start();
}
