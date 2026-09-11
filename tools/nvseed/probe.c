/* Isolated, single-threaded NV lifecycle experiment, not a product RNG API.
 * Build with --wrap=fork. No raw clone/syscall fork, threads or daemon helpers.
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

#define RECORD 160
#define SEED_OFFSET 96
#define DIGEST_OFFSET 128
#define REQUEST_LIMIT 1024
static int dirfd_ = -1, lockfd_ = -1, committed, ready, poisoned;
static unsigned long generation, requests;
static pid_t owner;
static unsigned char binding[64];
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;

/* A fork child is permanently poisoned until exec, independently of PID reuse.
 * Link wrapping covers all fork references in this executable. Direct clone,
 * vfork, raw syscalls and later linked libraries that fork are out of contract. */
pid_t __real_fork(void);
pid_t __wrap_fork(void)
{
    pid_t p = __real_fork();
    if (p == 0) {
        poisoned = 1; ready = 0;
        mbedtls_platform_zeroize(&drbg, sizeof(drbg));
        mbedtls_platform_zeroize(&entropy, sizeof(entropy));
        if (lockfd_ >= 0) close(lockfd_);
        lockfd_ = -1;
    }
    return p;
}

static int stage(const char *name)
{
    /* Events carry no state bytes. Fault injection exists only in host builds. */
    printf("stage=%s\n", name); fflush(stdout);
#ifdef NV_FIXTURE
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
#ifdef NV_FIXTURE
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
#ifndef NV_FIXTURE
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
    if (strcmp(identity, mac)) return -1;
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
    if (fcntl(lockfd_, F_SETLKW, &l)) return -1;
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
    fd = open("state", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (!metadata(fd, 0) && !fstat(fd, &s) && s.st_size == RECORD &&
        read(fd, r, RECORD) == RECORD && !memcmp(r, "4VRSNV01", 8) &&
        !memcmp(r + 16, binding, 64) && !mbedtls_sha256(r, DIGEST_OFFSET, hash, 0) &&
        !memcmp(hash, r + DIGEST_OFFSET, 32)) ret = 0;
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

static int persist(const unsigned char *seed, int provision)
{
    unsigned char r[RECORD]; int fd = -1, ret = -1;
    struct stat s;
    memset(r, 0, sizeof(r));
    if (generation >= 0xffffffffUL) goto done;
    /* Caller has recovered uncommitted pending under the exclusive lock. */
    if (lstat("pending", &s) == 0 || errno != ENOENT) goto done;
    if (provision && (lstat("state", &s) == 0 || errno != ENOENT)) goto done;
    memcpy(r, "4VRSNV01", 8); put32(r + 8, generation + 1);
    memcpy(r + 16, binding, 64); memcpy(r + SEED_OFFSET, seed, 32);
    if (mbedtls_sha256(r, DIGEST_OFFSET, r + DIGEST_OFFSET, 0)) goto done;
    fd = open("pending", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0 || metadata(fd, 0) || stage("created")) goto done;
    if (stage("before-write") || write(fd, r, sizeof(r)) != sizeof(r) || stage("written")) goto done;
    if (stage("before-file-fsync") || fsync(fd) || stage("file-fsync")) goto done;
    if (close(fd)) { fd = -1; goto done; } fd = -1;
    if (stage("before-rename") || rename("pending", "state") || stage("renamed")) goto done;
    if (stage("before-directory-fsync") || fsync(dirfd_) || stage("directory-fsync")) goto done;
    generation++; committed = 1; ret = 0;
done:
    if (fd >= 0) close(fd);
    mbedtls_platform_zeroize(r, sizeof(r));
    return ret;
}

static int nv_write(unsigned char *buf, size_t n)
{ return poisoned || ready || n != 32 || persist(buf, 0) ? -1 : (int)n; }

static int entropy_once(void *ctx, unsigned char *out, size_t n)
{
    if (ready || poisoned) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    return mbedtls_entropy_func(ctx, out, n);
}

static int runtime(unsigned char *out, size_t n)
{
    int ret = -1;
    if (ready && !poisoned && owner == getpid() && committed && requests < REQUEST_LIMIT && n <= 1024) {
        ret = mbedtls_ctr_drbg_random(&drbg, out, n);
        if (!ret) requests++;
    }
    if (ret) { mbedtls_platform_zeroize(out, n); ready = 0; }
    return ret;
}

int main(int argc, char **argv)
{
    unsigned char bytes[32], extra; struct rlimit limit;
    int rc = 1, initialized = 0, status; pid_t child;
    const unsigned char perso[] = "4vrs-nv-prototype-runtime-v1";
    memset(bytes, 0, sizeof(bytes)); umask(077);
    limit.rlim_cur = limit.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &limit)) return 1;
    limit.rlim_cur = limit.rlim_max = 30;
    if (setrlimit(RLIMIT_CPU, &limit)) return 1;
    if (setpriority(PRIO_PROCESS, 0, 10)) return 1;
    errno = 0;
    status = getpriority(PRIO_PROCESS, 0);
    if (errno || status < 10) return 1;
    alarm(30);
    if (argc != 4 || (strcmp(argv[1], "provision") && strcmp(argv[1], "consume") && strcmp(argv[1], "fork-test") && strcmp(argv[1], "limit-test"))) {
        fprintf(stderr, "usage: probe provision|consume|fork-test|limit-test ABS_PRIVATE_CF_DIR EXPECTED_DEVICE_ID\n"); return 2;
    }
#ifndef NV_FIXTURE
    if (strncmp(argv[2], "/var/hda/", 9)) goto done;
#endif
    if (enter_directory(argv[2]) || cf_binding(argv[3]) || lock_state() || recover_pending()) goto done;
    if (!strcmp(argv[1], "provision")) {
        /* Exactly 32 confidential bytes on stdin, never argv/env/log/file input.
         * The producer closes its pipe. Short/extra input fails closed. */
        size_t used = 0; ssize_t n;
        while (used < sizeof(bytes)) {
            n = read(STDIN_FILENO, bytes + used, sizeof(bytes) - used);
            if (n <= 0) goto done;
            used += (size_t)n;
        }
        if (read(STDIN_FILENO, &extra, 1) != 0 || persist(bytes, 1)) goto done;
        puts("provision=pass"); rc = 0; goto done;
    }
    mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg); initialized = 1;
    if (mbedtls_platform_set_nv_seed(nv_read, nv_write)) goto done;
    if (mbedtls_ctr_drbg_seed(&drbg, entropy_once, &entropy, perso, sizeof(perso)-1) || !committed) goto done;
    /* Never retry a failed entropy context: 3.6.7 sets initial_entropy_run
     * before its NV write. Always destroy it on any initialization failure. */
    ready = 1; owner = getpid();
    mbedtls_ctr_drbg_set_reseed_interval(&drbg, REQUEST_LIMIT);
    if (close(lockfd_)) { lockfd_ = -1; goto done; } lockfd_ = -1;
    if (runtime(bytes, sizeof(bytes)) || stage("runtime-output")) goto done;
    mbedtls_platform_zeroize(bytes, sizeof(bytes));
    if (!strcmp(argv[1], "fork-test")) {
        child = fork(); if (child < 0) goto done;
        if (child == 0) {
            /* Simulate PID equality: poison must still deny inherited RNG. */
            owner = getpid();
            _exit(runtime(bytes, sizeof(bytes)) < 0 && poisoned ? 0 : 1);
        }
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) goto done;
        if (runtime(bytes, sizeof(bytes))) goto done;
        puts("fork-exclusion=pass");
    }
    if (!strcmp(argv[1], "limit-test")) {
        while (requests < REQUEST_LIMIT) if (runtime(bytes, sizeof(bytes))) goto done;
        if (runtime(bytes, sizeof(bytes)) == 0) goto done;
        puts("request-limit=pass");
    }
    printf("consume=pass generation=%lu\n", generation); rc = 0;
done:
    ready = 0; mbedtls_platform_zeroize(bytes, sizeof(bytes));
    if (initialized) { mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy); }
    if (lockfd_ >= 0) close(lockfd_);
    if (dirfd_ >= 0) close(dirfd_);
    if (rc) fputs("operation=fail-closed\n", stderr);
    return rc;
}
