"""Allowlisted Gateway package validation and fd-relative, additive CF staging."""
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import tarfile

# The qualified Web candidate is immutable; update this pin only with a qualified delivery.
ARCHIVE_SHA256 = 'f519719cadca03b98ae00870581068961aee450ba79b60c57a5c893d2a471690'
PAYLOAD = {'4vrs-install', '4vrs-gateway', '4vrs-gateway.init',
           '4vrs-networking-wrapper', '4vrs-web', '4vrs-rng', '4vrs-kdf'}
NAMES = PAYLOAD | {'manifest.json', 'SHA256SUMS', 'LICENSE.mbedtls', 'NOTICE'}


def check(ok):
    if not ok:
        raise ValueError('Gateway package or staging validation failed')


def load(path):
    check(hashlib.sha256(Path(path).read_bytes()).hexdigest() == ARCHIVE_SHA256)
    files = {}
    with tarfile.open(path, 'r:gz') as archive:
        for item in archive:
            check(item.isfile() and item.name == '4vrs-gateway-v2026.02.01/' + Path(item.name).name)
            name = Path(item.name).name
            check(name in NAMES and name not in files and 0 < item.size < 8*1024*1024)
            files[name] = (archive.extractfile(item).read(), 0o755 if name in PAYLOAD else 0o644)
    check(set(files) == NAMES)
    manifest = json.loads(files['manifest.json'][0])
    check(manifest['format'] == 3 and manifest['version'] == 'v2026.02.01')
    check({i['name'] for i in manifest['files']} == PAYLOAD)
    for item in manifest['files']:
        data, mode = files[item['name']]
        check(len(data) == item['size'] and hashlib.sha256(data).hexdigest() == item['sha256'] and item['mode'] == '0755')
    for line in files['SHA256SUMS'][0].decode().splitlines():
        digest, name = line.split('  ')
        check(name in files and hashlib.sha256(files[name][0]).hexdigest() == digest)
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


def verify(fd, files):
    check(set(os.listdir(fd)) == set(files))
    for name, (data, mode) in files.items():
        f = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=fd)
        try:
            s = os.fstat(f)
            check(stat.S_ISREG(s.st_mode) and s.st_nlink == 1 and s.st_uid == s.st_gid == 0)
            check(stat.S_IMODE(s.st_mode) == mode and s.st_size == len(data))
            check(os.read(f, len(data)+1) == data)
        finally:
            os.close(f)


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
            for item, (data, mode) in sorted(files.items()):
                f = os.open(item, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, mode, dir_fd=fd)
                try:
                    os.fchmod(f, mode)
                    with os.fdopen(os.dup(f), 'wb') as stream:
                        stream.write(data)
                        stream.flush()
                    os.fsync(f)
                finally:
                    os.close(f)
            os.fsync(fd)
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
