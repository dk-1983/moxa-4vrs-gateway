"""Offline CF preparation for the explicitly confirmed Moxa #1, Ubuntu 24.04.
No mount, format, fsck, RO changes, UART, network access or secret handling here.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import resource
import stat
import struct
import subprocess
import sys

ADDRESS = '10.0.2.13:622'
MAC = '00:90:e8:1f:4c:f1'
BLKGETSIZE64 = 0x80081272
BLKROGET = 0x125e


def require(ok, message):
    if not ok:
        raise ValueError(message)


def superblock(data):
    require(len(data) == 1024 and data[56:58] == b'\x53\xef', 'ext3 superblock required')
    compat, incompat, rocompat = struct.unpack_from('<III', data, 92)
    require(compat & 4 and not incompat & ~6 and not rocompat & ~7, 'unsupported ext3 features')
    state = struct.unpack_from('<H', data, 58)[0]
    require(not state & 2, 'filesystem has recorded errors; no repair is performed')
    uuid = data[104:120].hex()
    require(uuid != '0' * 32, 'zero filesystem UUID')
    count = struct.unpack_from('<I', data, 4)[0]
    shift = struct.unpack_from('<I', data, 24)[0]
    require(count > 0 and shift <= 2, 'unsupported ext3 geometry')
    return dict(uuid=uuid, filesystem_bytes=count*(1024 << shift), clean=bool(state & 1) and not bool(incompat & 4),
                compat=compat, incompat=incompat, rocompat=rocompat)


def mbr(data, offset, length):
    require(len(data) == 512 and data[510:] == b'\x55\xaa', 'MBR required')
    entries = [data[446+i*16:462+i*16] for i in range(4)]
    active = [e for e in entries if any(e)]
    require(len(active) == 1, 'exactly one MBR partition required')
    e = active[0]; start, count = struct.unpack_from('<II', e, 8)
    require(e[0] in (0, 128) and e[4] == 0x83 and start*512 == offset and count*512 == length,
            'partition table does not match selected partition')
    return data[440:444].hex()


def unescape(text):
    return re.sub(r'\\([0-7]{3})', lambda m: chr(int(m[1], 8)), text)


def mounts(text, device, mountpoint, write):
    records = []
    for line in text.splitlines():
        left, right = line.split(' - ', 1); a = left.split(); b = right.split()
        if a[2] != device:
            continue
        records.append((a, b))
    require(len(records) == 1, 'partition must have exactly one mount in this namespace')
    a, b = records[0]; flags = set(a[5].split(',')); options = set(b[2].split(','))
    require(unescape(a[3]) == '/' and unescape(a[4]) == mountpoint and b[0] == 'ext3', 'whole ext3 partition mount required')
    require({'nodev', 'nosuid', 'noexec'}.issubset(flags), 'mount requires nodev,nosuid,noexec')
    require(('rw' if write else 'ro') in flags, 'mount access mode differs from operation')
    if not write:
        require(bool({'noload', 'norecovery'} & options), 'read-only inspection requires journal replay disabled')
    else:
        require(not {'noload', 'norecovery'} & options, 'write mount must use the journal')


def trusted_directory(path):
    require(path.is_absolute(), 'absolute mount path required')
    for p in [*reversed(path.parents), path]:
        s = p.lstat()
        require(stat.S_ISDIR(s.st_mode) and s.st_uid == 0 and s.st_gid == 0 and not s.st_mode & 0o022,
                'mount path must have root-owned, non-writable, non-symlink ancestors')


def target_confirmation(doc, media, require_confirmed=True):
    require(doc.get('confirmed') is True or (not require_confirmed and doc.get('confirmed') is False),
            'confirm binding only after fresh read-only inspection')
    require(doc.get('address') == ADDRESS and doc.get('mac', '').lower() == MAC,
            'fresh explicit Moxa #1 identity confirmation required')
    uuid = doc.get('cf_uuid', '').replace('-', '').lower()
    require(re.fullmatch('[0-9a-f]{32}', uuid) and uuid == media['uuid'], 'target CF UUID mismatch')
    require(doc.get('disk_bytes') == media['disk_bytes'] and doc.get('partition_bytes') == media['partition_bytes'],
            'target/card geometry mismatch')


def capture(disk, partition, mountpoint, write):
    """Return pinned descriptors; failure closes everything before returning."""
    fds = []
    try:
        trusted_directory(mountpoint)
        for p in [disk, partition]:
            require(p.is_absolute() and not p.is_symlink(), 'use the freshly identified direct device path')
            fd = os.open(p, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC); fds.append(fd)
            require(stat.S_ISBLK(os.fstat(fd).st_mode), 'physical block device required; image paths are not a production mode')
        dfd, pfd = fds
        dev = lambda fd: f'{os.major(os.fstat(fd).st_rdev)}:{os.minor(os.fstat(fd).st_rdev)}'
        ds = Path('/sys/dev/block', dev(dfd)).resolve(strict=True)
        ps = Path('/sys/dev/block', dev(pfd)).resolve(strict=True)
        require(ps.parent == ds and not (ds/'partition').exists() and (ps/'partition').exists(), 'disk/partition topology mismatch')
        require(len([p for p in ds.iterdir() if (p/'partition').exists()]) == 1, 'disk must have exactly one partition')
        require(not list((ds/'holders').iterdir()) and not list((ps/'holders').iterdir()), 'device has holders')
        reader = None
        for p in ps.parents:
            if (p/'idVendor').is_file() and (p/'idProduct').is_file():
                reader = (p/'idVendor').read_text().strip()+':'+(p/'idProduct').read_text().strip()
                break
        require(reader == '05e3:0743', 'unconfirmed physical CF reader')
        size = lambda fd: struct.unpack('Q', fcntl.ioctl(fd, BLKGETSIZE64, bytes(8)))[0]
        readonly = lambda fd: struct.unpack('i', fcntl.ioctl(fd, BLKROGET, bytes(4)))[0]
        disk_bytes, part_bytes = size(dfd), size(pfd)
        dro, pro = readonly(dfd), readonly(pfd)
        require((dro, pro) == ((0, 0) if write else (1, 1)), 'disk and partition RO flags differ from approved phase; never cleared automatically')
        offset = int((ps/'start').read_text())*512
        require(offset > 0 and offset+part_bytes <= disk_bytes, 'invalid partition geometry')
        sb = superblock(os.pread(pfd, 1024, 1024))
        require(sb['filesystem_bytes'] <= part_bytes, 'filesystem exceeds partition')
        if not write:
            require(sb['clean'], 'unclean filesystem; stop, no journal replay or repair')
        disk_id = mbr(os.pread(dfd, 512, 0), offset, part_bytes)
        mounttext = Path('/proc/self/mountinfo').read_text()
        require(not any(line.split()[2] == dev(dfd) for line in mounttext.splitlines()), 'whole disk is mounted')
        mounts(mounttext, dev(pfd), str(mountpoint), write)
        root = os.open(mountpoint, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC); fds.append(root)
        rs = os.fstat(root)
        require(rs.st_dev == os.fstat(pfd).st_rdev and rs.st_ino == 2, 'mount changed or does not start at filesystem root')
        try:
            state = os.stat('4vrs-rng', dir_fd=root, follow_symlinks=False)
            rng = 'present-do-not-reinitialize'
        except FileNotFoundError:
            rng = 'absent'
        doc = dict(schema=1, address=ADDRESS, mac=MAC, disk=str(disk), partition=str(partition), mount=str(mountpoint),
                   disk_sysfs=str(ds), partition_sysfs=str(ps), reader=reader, disk_bytes=disk_bytes,
                   partition_bytes=part_bytes, offset=offset, disk_id=disk_id, uuid=sb['uuid'], rng=rng,
                   clean_readonly_inspection=not write, disk_ro=dro, partition_ro=pro)
        return doc, (root, pfd, dfd)
    except BaseException:
        for fd in fds:
            os.close(fd)
        raise


def compare(receipt, current):
    require(receipt.get('schema') == 1 and receipt.get('clean_readonly_inspection') is True, 'clean read-only receipt required')
    keys = ['address','mac','disk','partition','mount','disk_sysfs','partition_sysfs','reader',
            'disk_bytes','partition_bytes','offset','disk_id','uuid']
    require(all(k in receipt and k in current and receipt[k] == current[k] for k in keys), 'media or binding changed since read-only inspection')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['inspect', 'prepare', 'verify'])
    for name in ['disk', 'partition', 'mount', 'target-confirmation', 'receipt']:
        p.add_argument('--'+name, required=True, type=Path)
    p.add_argument('--authorize-write', action='store_true')
    args = p.parse_args(); fds = ()
    try:
        require(os.geteuid() == 0, 'root required')
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        write = args.mode == 'prepare'
        require(not write or args.authorize_write, 'explicit --authorize-write required')
        doc, fds = capture(args.disk, args.partition, args.mount, write)
        confirmation = json.loads(args.target_confirmation.read_text(encoding='utf-8'))
        target_confirmation(confirmation, doc, require_confirmed=args.mode != 'inspect')
        if args.mode == 'inspect':
            require(not args.receipt.resolve().is_relative_to(args.mount.resolve()), 'receipt must be stored on PC, outside CF')
            with args.receipt.open('x', encoding='utf-8') as f:
                json.dump(doc, f, indent=2); f.write('\n')
            print(json.dumps(doc, indent=2)); return 0
        receipt = json.loads(args.receipt.read_text(encoding='utf-8')); compare(receipt, doc)
        if write:
            require(receipt.get('rng') == 'absent' and doc['rng'] == 'absent', 'RNG directory exists; no repair, deletion or retry')
        worker = Path(__file__).resolve().with_name('4vrs-cf-rng-worker')
        command = [str(worker), args.mode, *map(str, fds), MAC, doc['uuid'], str(doc['disk_bytes']), str(doc['partition_bytes']), 'confirmed-moxa1']
        result = subprocess.run(command, pass_fds=fds, stdin=subprocess.DEVNULL, env={'PATH':'/usr/bin:/bin'}, timeout=60)
        return result.returncode
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if isinstance(error, ValueError) and not isinstance(error, json.JSONDecodeError):
            print('Refused: '+str(error), file=sys.stderr)
        print('CF operation refused or not confirmed. Preserve all files; inspect/verify before any further action.', file=sys.stderr)
        return 1
    finally:
        for fd in fds:
            os.close(fd)


if __name__ == '__main__':
    sys.exit(main())
