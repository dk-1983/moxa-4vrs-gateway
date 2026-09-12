"""Allowlisted Gateway package validation and fd-relative, additive CF staging."""
import hashlib
import io
import json
import re
import os
from pathlib import Path
import stat
import struct
import tarfile

# The qualified Web candidate is immutable; update this pin only with a qualified delivery.
ARCHIVE_SHA256 = '3e911f67b2f847b881121863357814a219e340afcabb01913241b48828983d22'
PAYLOAD = {'4vrs-install', '4vrs-gateway', '4vrs-gateway.init',
           '4vrs-networking-wrapper', '4vrs-web', '4vrs-rng', '4vrs-kdf'}
NAMES = PAYLOAD | {'manifest.json', 'SHA256SUMS', 'LICENSE.mbedtls', 'NOTICE'}


def check(ok):
    if not ok:
        raise ValueError('Gateway package or staging validation failed')


def check_target(data, version, platform="linux26"):
    profiles = {"linux26": (0x04000002, b"/lib/ld-linux.so.3\0"), "linux24": (0x202, b"/lib/ld-linux.so.2\0")}
    if platform not in profiles: raise ValueError("unknown platform")
    target_flags, target_loader = profiles[platform]
    """Basic ELF envelope check, not a substitute for the full target ABI audit."""
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x02\x01":
        raise ValueError("expected ELF32 big-endian executable")
    kind, machine, elf_version = struct.unpack_from(">HHI", data, 16)
    phoff = struct.unpack_from(">I", data, 28)[0]
    flags = struct.unpack_from(">I", data, 36)[0]
    ehsize, phsize, phnum = struct.unpack_from(">HHH", data, 40)
    if (kind != 2 or machine != 40 or elf_version != 1 or ehsize != 52
            or flags != target_flags or phsize != 32 or not 1 <= phnum <= 128
            or phoff < 52 or phoff + phnum * phsize > len(data)):
        raise ValueError("unsupported target ELF envelope")
    interpreters = []
    loadable = False
    for index in range(phnum):
        ptype, offset, _, _, filesz, memsz, _, _ = struct.unpack_from(
            ">IIIIIIII", data, phoff + index * phsize)
        if offset + filesz > len(data) or (ptype == 1 and filesz > memsz):
            raise ValueError("truncated or invalid ELF segment")
        if ptype == 1:
            loadable = True
        if ptype == 3:
            interpreters.append(data[offset:offset + filesz])
    if not loadable or interpreters != [target_loader]:
        raise ValueError("unsupported target loader")
    versions = set(re.findall(rb"v[0-9]{4}\.[0-9]{2,}\.[0-9]{2,}\x00", data))
    if versions != {version.encode("ascii") + b"\0"}:
        raise ValueError("embedded version does not match package version")


def check_script(data):
    if (not data.startswith(b"#!/bin/sh\n") or b"\r" in data
            or b"\0" in data or not data.endswith(b"\n")):
        raise ValueError("scripts must use /bin/sh, LF and a final newline")
    data.decode("ascii")


VERSION = 'v2026.02.03'
BUNDLE_NAMES = {'install.sh', 'README.txt', 'SHA256SUMS'} | {
    'platforms/' + platform + '/' + name
    for platform in ('linux24', 'linux26') for name in NAMES}


def checksums(files, prefix, expected):
    seen = set()
    for line in files[prefix + 'SHA256SUMS'][0].decode('ascii').splitlines():
        digest, name = line.split('  ')
        check(name in expected and name not in seen)
        check(hashlib.sha256(files[prefix + name][0]).hexdigest() == digest)
        seen.add(name)
    check(seen == expected)


def load(path):
    raw = Path(path).read_bytes()
    check(0 < len(raw) <= 32*1024*1024 and hashlib.sha256(raw).hexdigest() == ARCHIVE_SHA256)
    files = {}
    prefix = '4vrs-gateway-' + VERSION + '-universal/'
    with tarfile.open(fileobj=io.BytesIO(raw), mode='r:gz') as archive:
        for item in archive:
            check(item.isfile() and item.name.startswith(prefix))
            name = item.name[len(prefix):]
            check(name in BUNDLE_NAMES and name not in files and 0 < item.size < 8*1024*1024)
            mode = 0o755 if name == 'install.sh' or name.rsplit('/', 1)[-1] in PAYLOAD else 0o644
            check(item.mode == mode)
            files[name] = (archive.extractfile(item).read(), mode)
    check(set(files) == BUNDLE_NAMES)
    checksums(files, '', BUNDLE_NAMES - {'SHA256SUMS'})
    check_script(files['install.sh'][0])
    for platform in ('linux24', 'linux26'):
        prefix = 'platforms/' + platform + '/'
        manifest = json.loads(files[prefix + 'manifest.json'][0])
        check(manifest['format'] == 3 and manifest['version'] == VERSION)
        check(manifest['product'] == '4VRS Gateway' and manifest['entrypoint'] == '4vrs-install')
        check(len(manifest['files']) == len(PAYLOAD) and {i['name'] for i in manifest['files']} == PAYLOAD)
        for item in manifest['files']:
            data, mode = files[prefix + item['name']]
            check(len(data) == item['size'] and hashlib.sha256(data).hexdigest() == item['sha256'] and item['mode'] == '0755')
            if item['name'].endswith(('.init', '-wrapper')): check_script(data)
            else: check_target(data, VERSION, platform)
        checksums(files, prefix, PAYLOAD | {'manifest.json'})
    return files


def directory(parent, name, create=False):
    if create:
        try:
            os.mkdir(name, 0o700, dir_fd=parent)
            os.fsync(parent)
        except FileExistsError:
            pass
    fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
    s = os.fstat(fd)
    if s.st_uid != 0 or s.st_gid != 0 or s.st_mode & 0o022:
        os.close(fd)
        check(False)
    return fd


def hierarchy(files):
    tree = {}
    for name, value in files.items():
        parts = name.split('/')
        check(all(p and p not in ('.', '..') and re.fullmatch(r'[A-Za-z0-9_.-]+', p) for p in parts))
        node = tree
        for part in parts[:-1]:
            child = node.setdefault(part, {})
            check(isinstance(child, dict))
            node = child
        check(parts[-1] not in node)
        node[parts[-1]] = value
    return tree


def verify_tree(fd, tree):
    check(set(os.listdir(fd)) == set(tree))
    for name, value in tree.items():
        if isinstance(value, dict):
            child = directory(fd, name)
            try: verify_tree(child, value)
            finally: os.close(child)
            continue
        data, mode = value
        f = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=fd)
        try:
            s = os.fstat(f)
            check(stat.S_ISREG(s.st_mode) and s.st_nlink == 1 and s.st_uid == s.st_gid == 0)
            check(stat.S_IMODE(s.st_mode) == mode and s.st_size == len(data))
            with os.fdopen(os.dup(f), 'rb') as stream: check(stream.read(len(data)+1) == data)
        finally: os.close(f)


def verify(fd, files):
    verify_tree(fd, hierarchy(files))


def write_tree(fd, tree):
    for name, value in sorted(tree.items()):
        if isinstance(value, dict):
            child = directory(fd, name, create=True)
            try: write_tree(child, value)
            finally: os.close(child)
            continue
        data, mode = value
        f = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, mode, dir_fd=fd)
        try:
            os.fchmod(f, mode)
            with os.fdopen(os.dup(f), 'wb') as stream:
                stream.write(data)
                stream.flush()
            os.fsync(f)
        finally: os.close(f)
    os.fsync(fd)


def stage(root, files, write=False):
    """Never overwrite active installation, RNG, previous packages or interrupted work."""
    name = 'gateway-' + ARCHIVE_SHA256[:16]
    parent = directory(root, '4vrs-packages', create=write)
    try:
        names = os.listdir(parent)
        check(name + '.pending' not in names)
        if name in names:
            fd = directory(parent, name)
            try:
                verify(fd, files)
            finally:
                os.close(fd)
            return '4vrs-packages/' + name
        check(write)
        os.mkdir(name + '.pending', 0o700, dir_fd=parent)
        os.fsync(parent)
        fd = directory(parent, name + '.pending')
        try:
            write_tree(fd, hierarchy(files))
            verify(fd, files)
            # Root-only parent, exclusive wizard lock; refuse an unexpected destination.
            check(name not in os.listdir(parent))
            os.rename(name + '.pending', name, src_dir_fd=parent, dst_dir_fd=parent)
            os.fsync(parent)
        finally:
            os.close(fd)
        return '4vrs-packages/' + name
    finally:
        os.close(parent)


def existing(root):
    """Public structure gate only. Native target installer performs schema/ABI migration."""
    app = directory(root, '4vrs')
    try:
        binaries = directory(app, 'bin')
        try:
            f = os.open('4vrs-gateway', os.O_RDONLY | os.O_NOFOLLOW, dir_fd=binaries)
            try:
                s = os.fstat(f)
                check(stat.S_ISREG(s.st_mode) and s.st_nlink == 1 and s.st_uid == s.st_gid == 0 and not s.st_mode & 0o022)
                header = os.read(f, 52)
                check(len(header) == 52 and header[:7] == b'\x7fELF\x01\x02\x01')
                check(struct.unpack_from('>HH', header, 16) == (2, 40))
            finally:
                os.close(f)
        finally:
            os.close(binaries)
        config = directory(app, 'config')
        try:
            f = os.open('gateway.conf', os.O_RDONLY | os.O_NOFOLLOW, dir_fd=config)
            try:
                s = os.fstat(f)
                check(stat.S_ISREG(s.st_mode) and s.st_nlink == 1 and s.st_uid == s.st_gid == 0 and not s.st_mode & 0o022)
                # Only public header is inspected; never include config contents in reports.
                header = os.read(f, 64).splitlines()
                check(len(header) >= 2 and header[0] == b'4VRS_GATEWAY_CONFIG' and header[1] in (b'schema=1', b'schema=2', b'schema=3'))
            finally:
                os.close(f)
        finally:
            os.close(config)
    finally:
        os.close(app)
