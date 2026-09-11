#!/usr/bin/env python3
"""Offline CompactFlash wizard for Ubuntu 24.04. No network or device access to Moxa."""
import argparse
import contextlib
import fcntl
import hashlib
import importlib.util
import ipaddress
import json
import os
from pathlib import Path
import re
import resource
import signal
import shutil
import stat
import struct
import subprocess
import tempfile

spec = importlib.util.spec_from_file_location('bootstrap', Path(__file__).with_name('cf-bootstrap.py'))
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)
package_spec = importlib.util.spec_from_file_location('cf_package', Path(__file__).with_name('cf-package.py'))
package = importlib.util.module_from_spec(package_spec)
package_spec.loader.exec_module(package)


class Refused(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Refused(message)


def target(name, address, mac):
    require(bool(re.fullmatch(r'[A-Za-z0-9_. -]{1,64}', name)), 'Invalid target name / неверное имя прибора')
    try:
        host, port = address.split(':')
        ipaddress.IPv4Address(host)
        require(port.isdecimal() and 1 <= int(port) <= 65535, 'Invalid port / неверный порт')
    except ValueError:
        raise Refused('Use IPv4:port / укажите IPv4:порт') from None
    mac = mac.lower()
    require(bool(re.fullmatch(r'(?:[0-9a-f]{2}:){5}[0-9a-f]{2}', mac)) and
            mac != '00:00:00:00:00:00' and not int(mac[:2], 16) & 1,
            'Unicast target MAC required / нужен unicast MAC целевого прибора')
    return dict(name=name, address=address, mac=mac)


def descendants(node):
    yield node
    for child in node.get('children', []):
        yield from descendants(child)


def disk_policy(node, owned_mount=None):
    """All mounted descendants (root/boot/home/swap, LVM/crypt/RAID) protect a disk."""
    if node.get('type') != 'disk' or node.get('tran') != 'usb':
        return 'Only USB card readers / только USB-картридеры'
    if int(node.get('size', 0)) < 32 * 1024 * 1024:
        return 'Invalid media size / неверный размер'
    for part in descendants(node):
        if part.get('type') not in ('disk', 'part'):
            return 'Mapped/system device / составной или системный диск'
        if part.get('ro'):
            return 'Read-only device; flags are never cleared / носитель RO'
        if any(m and m != owned_mount for m in part.get('mountpoints', [])):
            return 'Mounted or swap: protected / смонтирован или swap: защищён'
    return None


def identity(doc):
    # Disk path/number is deliberately absent from the operator's persistent identity.
    public = {k: doc[k] for k in ('reader', 'serial', 'disk_bytes')}
    public['partitions'] = [{k: p.get(k) for k in ('size', 'uuid', 'fstype', 'start')} for p in doc['partitions']]
    return hashlib.sha256(json.dumps(public, sort_keys=True).encode()).hexdigest()[:16]


def confirmation(action, doc):
    return f"{action} {doc['reader']} {doc['serial']} {doc['disk_bytes']} {identity(doc)}"


def display_disks(disks, show=print):
    for d in disks:
        show(f"\nID {d['id']} | {d['disk_bytes']} bytes ({d['disk_bytes']/2**30:.2f} GiB)")
        show('Device / устройство: ' + json.dumps(d['disk']))
        show('USB / serial: ' + json.dumps(d['reader'] + ' / ' + d['serial']))
        for p in d['partitions']:
            show('Partition / раздел: ' + json.dumps({k: p.get(k) for k in ('name','size','fstype','uuid','mountpoints')}, ensure_ascii=True))
        if not d['partitions']:
            show('No partitions / нет разделов')
        show('BLOCKED / ЗАЩИЩЁН: ' + d['blocked'] if d['blocked'] else 'Available for inspection / доступен для проверки')


def inspect_headers(disk, part, size, offset, uuid):
    try:
        sb = bootstrap.superblock(os.pread(part, 1024, 1024))
        bootstrap.mbr(os.pread(disk, 512, 0), offset, size)
    except ValueError:
        raise Refused('Incompatible ext3/MBR / несовместимая ext3 или MBR') from None
    require(sb['clean'], 'needs_recovery / unclean: STOP; update refused; no journal replay or fsck repair / остановка без исправления')
    require(sb['filesystem_bytes'] <= size and sb['uuid'] == (uuid or '').replace('-', '').lower(),
            'Filesystem geometry/UUID mismatch / несоответствие UUID или размера')
    return sb


def mkfs_command(path):
    return ['mke2fs', '-t', 'ext3', '-b', '4096', '-I', '128', '-m', '0',
            '-O', 'none,has_journal,filetype,sparse_super,large_file',
            '-E', 'nodiscard,lazy_itable_init=0,lazy_journal_init=0', str(path)]


PARTITION_RECIPE = b'label: dos\nunit: sectors\n\nstart=2048, type=83\n'


def command(args, *, input=None, pass_fds=(), allowed=(0,)):
    # No external output (fsck can mention filenames) is copied into logs or reports.
    result = subprocess.run(list(map(str, args)), input=input, capture_output=True,
                            pass_fds=pass_fds, env={'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'LC_ALL': 'C'}, timeout=180)
    require(result.returncode in allowed, 'Operation failed / операция не подтверждена: ' + Path(args[0]).name)
    return result.stdout


def validate_installation(directory):
    """Fail before any mount/format if the supplied worker or helper is missing/changed."""
    bootstrap.trusted_directory(directory)
    names = ['manifest.json', 'cf-wizard.py', 'cf-bootstrap.py', 'cf-package.py', 'gateway.tar.gz', '4vrs-cf-rng-worker']
    for name in names:
        s = (directory/name).lstat()
        require(stat.S_ISREG(s.st_mode) and s.st_uid == 0 and s.st_gid == 0 and not s.st_mode & 0o022,
                'Kit files must be trusted/root-owned / файлы комплекта должны принадлежать root')
    manifest = json.loads((directory/'manifest.json').read_text())
    require(manifest.get('format') == 1 and manifest.get('generic_target') is True, 'Wrong wizard kit / неверный комплект')
    for name in names[1:]:
        require(hashlib.sha256((directory/name).read_bytes()).hexdigest() == manifest.get('files', {}).get(name, {}).get('sha256'),
                'Kit checksum mismatch / контрольная сумма комплекта не совпадает')
    require(os.access(directory/'4vrs-cf-rng-worker', os.X_OK), 'Worker is not executable / worker не исполняемый')


def refresh_partition(part):
    # lsblk may show cached udev UUIDs immediately after mke2fs. Probe the selected
    # eligible medium directly; do not accept the cache as its write identity.
    fields = {}
    for line in command(['blkid', '-p', '-o', 'export', part['name']], allowed=(0, 2)).splitlines():
        if b'=' in line:
            key, value = line.split(b'=', 1)
            if key in (b'UUID', b'TYPE'):
                fields[key.decode('ascii')] = value.decode('ascii')
    part['uuid'] = fields.get('UUID')
    part['fstype'] = fields.get('TYPE')


class Linux:
    def __init__(self):
        self.mountpoint = None
        self.partition_dev = None
        self.fds = []

    def scan(self):
        raw = json.loads(command(['lsblk', '--json', '--bytes', '--paths', '--output',
                                  'NAME,TYPE,TRAN,SIZE,RO,FSTYPE,UUID,START,MOUNTPOINTS,MAJ:MIN']))
        root_rows = [x.split()[2] for x in Path('/proc/self/mountinfo').read_text().splitlines()
                     if bootstrap.unescape(x.split()[4]) == '/']
        visible = {n['maj:min'] for tree in raw['blockdevices'] for n in descendants(tree)}
        system_known = len(root_rows) == 1 and root_rows[0] in visible
        result = []
        for node in raw['blockdevices']:
            if node['type'] != 'disk':
                continue
            ds = Path('/sys/dev/block', node['maj:min']).resolve(strict=True)
            reader = serial = ''
            for parent in [ds, *ds.parents]:
                if (parent/'idVendor').is_file() and (parent/'idProduct').is_file():
                    reader = (parent/'idVendor').read_text().strip() + ':' + (parent/'idProduct').read_text().strip()
                    if (parent/'serial').is_file():
                        serial = (parent/'serial').read_text().strip()
                    break
            reason = disk_policy(node, self.mountpoint)
            if not system_known:
                reason = 'System disk cannot be resolved; use native Ubuntu / системный диск не определён'
            nodes = list(descendants(node))
            if any(list((Path('/sys/dev/block', n['maj:min']).resolve()/'holders').iterdir()) for n in nodes):
                reason = 'Device has holders / носитель занят другим устройством'
            if not re.fullmatch('[0-9a-f]{4}:[0-9a-f]{4}', reader) or not re.fullmatch('[A-Za-z0-9_.-]{1,128}', serial):
                reason = 'No stable USB reader identity / нет устойчивого USB ID и serial'
            parts = [n for n in nodes if n['type'] == 'part']
            if not reason:
                for part in parts:
                    refresh_partition(part)
            doc = dict(disk=node['name'], dev=node['maj:min'], sysfs=str(ds), reader=reader, serial=serial,
                       disk_bytes=int(node['size']), partitions=parts, blocked=reason)
            doc['id'] = identity(doc)
            result.append(doc)
        # Readers with identical serials cannot uniquely select a card.
        for doc in result:
            if sum((d['reader'], d['serial']) == (doc['reader'], doc['serial']) for d in result) != 1:
                doc['blocked'] = 'Ambiguous reader serial / неоднозначный serial'
        return result

    def recheck(self, expected):
        matches = [d for d in self.scan() if identity(d) == expected['id']]
        require(len(matches) == 1 and not matches[0]['blocked'], 'Media busy, replaced or changed / носитель занят или изменён')
        current = matches[0]
        require(all(current[k] == expected[k] for k in ('disk', 'dev', 'sysfs')),
                'Device path changed; restart inspection / путь изменился, начните заново')
        return current

    def open_block(self, path, expected_size):
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
        self.fds.append(fd)
        require(stat.S_ISBLK(os.fstat(fd).st_mode), 'Physical block device required / нужен блочный носитель')
        actual = struct.unpack('Q', fcntl.ioctl(fd, bootstrap.BLKGETSIZE64, bytes(8)))[0]
        require(actual == expected_size, 'Media geometry changed / изменилась геометрия')
        return fd

    def preflight(self, doc):
        self.recheck(doc)
        disk = self.open_block(doc['disk'], doc['disk_bytes'])
        parts = doc['partitions']
        if not parts:
            require(not command(['wipefs', '--no-act', '--noheadings', '--output', 'TYPE', doc['disk']]).strip(),
                    'Unpartitioned filesystem/signature; manual review required / разбор сигнатур вручную')
            return None
        require(len(parts) == 1 and parts[0]['fstype'] == 'ext3', 'One ext3 MBR partition required / нужен один раздел ext3 MBR')
        p = parts[0]
        part = self.open_block(p['name'], int(p['size']))
        ps = Path('/sys/dev/block', p['maj:min']).resolve(strict=True)
        require(ps.parent == Path(doc['sysfs']), 'Partition topology mismatch / неверная принадлежность раздела')
        offset = int((ps/'start').read_text()) * 512
        require(offset > 0 and offset + int(p['size']) <= doc['disk_bytes'], 'Partition exceeds disk / раздел выходит за пределы диска')
        sb = inspect_headers(disk, part, int(p['size']), offset, p['uuid'])
        self.recheck(doc)
        command(['e2fsck', '-f', '-n', p['name']])
        return dict(uuid=sb['uuid'], partition_bytes=int(p['size']), pfd=part, dfd=disk, part=p['name'], dev=p['maj:min'])

    def mount(self, doc, media, write=False):
        self.recheck(doc)
        # The clean gate is repeated immediately before every initial write mount.
        require(bootstrap.superblock(os.pread(media['pfd'], 1024, 1024))['clean'],
                'Filesystem not clean; mount refused / нечистая ФС, монтирование запрещено')
        if self.mountpoint is None:
            self.mountpoint = tempfile.mkdtemp(prefix='moxa-cf-', dir='/run')
        self.partition_dev = media['dev']
        opts = ('rw' if write else 'ro,noload') + ',nodev,nosuid,noexec'
        command(['mount', '-t', 'ext3', '-o', opts, f"/proc/self/fd/{media['pfd']}", self.mountpoint], pass_fds=(media['pfd'],))
        bootstrap.mounts(Path('/proc/self/mountinfo').read_text(), media['dev'], self.mountpoint, write)
        root = os.open(self.mountpoint, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
        self.fds.append(root)
        require(os.fstat(root).st_dev == os.fstat(media['pfd']).st_rdev and os.fstat(root).st_ino == 2,
                'Mount changed / изменилось монтирование')
        return root

    def unmount(self):
        if not self.mountpoint:
            return
        for fd in list(self.fds):
            if stat.S_ISDIR(os.fstat(fd).st_mode):
                os.close(fd)
                self.fds.remove(fd)
        rows = [x for x in Path('/proc/self/mountinfo').read_text().splitlines()
                if bootstrap.unescape(x.split()[4]) == self.mountpoint]
        if rows:
            require(len(rows) == 1 and rows[0].split()[2] == self.partition_dev,
                    'Mount replaced; manual unmount required / mount подменён, остановка')
            command(['umount', self.mountpoint])  # Never lazy or forced.

    def inventory(self, root):
        names = os.listdir(root)
        fs = os.fstatvfs(root)
        # Display only root entry names, safely escaped; never traverse or read RNG files.
        return dict(used_bytes=(fs.f_blocks-fs.f_bfree)*fs.f_frsize, entries=len(names),
                    names=sorted(names)[:64], rng='4vrs-rng' in names,
                    system_tree='etc' in names and bool({'usr', 'boot', 'bin'} & set(names)))

    def worker(self, mode, doc, media, root, chosen):
        self.recheck(doc)
        worker = Path(__file__).resolve().with_name('4vrs-cf-rng-worker')
        bootstrap.trusted_directory(worker.parent)
        s = worker.lstat()
        require(stat.S_ISREG(s.st_mode) and s.st_uid == 0 and s.st_gid == 0 and not s.st_mode & 0o022,
                'Worker must be trusted/root-owned / worker должен принадлежать root')
        output = command([worker, mode, root, media['pfd'], media['dfd'], chosen['mac'], media['uuid'],
                          doc['disk_bytes'], media['partition_bytes'], 'confirmed-target'],
                         pass_fds=(root, media['pfd'], media['dfd']))
        require(bool(re.fullmatch(rb'state=(?:valid|prepared) generation=[0-9]+(?: policy=required)?\n', output)),
                'Worker result not confirmed / результат worker не подтверждён')

    def format(self, doc):
        self.write_topology(doc)
        self.recheck(doc)
        # Exact compatible DOS table; no discard and no defaults-dependent ext4 features.
        require(doc['disk_bytes'] < 2**41, 'MBR size limit / превышен предел MBR')
        command(['sfdisk', '--lock=yes', '--wipe=always', '--wipe-partitions=always', doc['disk']],
                input=PARTITION_RECIPE)
        command(['udevadm', 'settle'])
        new = [d for d in self.scan() if d['sysfs'] == doc['sysfs'] and d['serial'] == doc['serial'] and
               d['reader'] == doc['reader'] and d['disk_bytes'] == doc['disk_bytes'] and d['disk'] == doc['disk']]
        require(len(new) == 1 and not new[0]['blocked'] and len(new[0]['partitions']) == 1,
                'Partition creation not confirmed / создание раздела не подтверждено')
        new = new[0]
        p = new['partitions'][0]
        self.write_topology(new)
        self.recheck(new)
        command(mkfs_command(p['name']))
        command(['udevadm', 'settle'])
        final = [d for d in self.scan() if d['sysfs'] == doc['sysfs'] and d['serial'] == doc['serial'] and
                 d['reader'] == doc['reader'] and d['disk_bytes'] == doc['disk_bytes']]
        require(len(final) == 1 and not final[0]['blocked'], 'Formatted medium changed / носитель изменился')
        return final[0]

    def write_topology(self, doc):
        """No old filesystem inspection/replay. Validate every partition before destruction."""
        previous = len(self.fds)
        try:
            self.recheck(doc)
            fd = self.open_block(doc['disk'], doc['disk_bytes'])
            require(os.fstat(fd).st_rdev == os.makedev(*map(int, doc['dev'].split(':'))), 'Disk identity mismatch')
            for p in doc['partitions']:
                ps = Path('/sys/dev/block', p['maj:min']).resolve(strict=True)
                require(ps.parent == Path(doc['sysfs']), 'Partition topology mismatch')
                start = int((ps/'start').read_text()) * 512
                require(start > 0 and start + int(p['size']) <= doc['disk_bytes'], 'Partition exceeds disk')
                pf = self.open_block(p['name'], int(p['size']))
                require(os.fstat(pf).st_rdev == os.makedev(*map(int, p['maj:min'].split(':'))), 'Partition identity mismatch')
            self.recheck(doc)
        finally:
            # Do not hold old partition handles across the kernel partition-table reread.
            for fd in self.fds[previous:]:
                os.close(fd)
            del self.fds[previous:]

    def close(self):
        try:
            self.unmount()
            if self.mountpoint:
                os.rmdir(self.mountpoint)
                self.mountpoint = None
        finally:
            for fd in self.fds:
                os.close(fd)
            self.fds = []

def execute(backend, doc, chosen, action, ask=input, show=print, files=None):
    """PC preparation only; the native installer owns activation and rollback on Moxa."""
    require(action in ('wipe', 'update'), 'Choose wipe or update')
    require(not doc['blocked'], doc['blocked'] or 'Protected disk')
    if files is None:
        files = package.load(Path(__file__).with_name('gateway.tar.gz'))
    try:
        if action == 'wipe':
            phrase = confirmation('ERASE-ALL-DATA', doc)
            show('DELETE ALL DATA / УДАЛЕНИЕ ВСЕХ ДАННЫХ: ' + phrase)
            require(ask('Type exactly / введите точно: ') == phrase, 'Not confirmed; no write')
            # Deliberately before preflight: old journal, signatures and RNG are discarded.
            doc = backend.format(doc)
            media = backend.preflight(doc)
            require(media is not None, 'No compatible ext3 after format')
        else:
            media = backend.preflight(doc)
            require(media is not None, 'Update requires an existing installation; choose wipe for a blank card')
            root = backend.mount(doc, media)
            inventory = backend.inventory(root)
            show('Existing data / существующие данные: ' + json.dumps(inventory, ensure_ascii=True))
            require(not inventory.get('system_tree'), 'System filesystem detected: protected')
            require(inventory['rng'], 'Existing RNG required for update; regeneration prohibited')
            backend.worker('verify', doc, media, root, chosen)
            package.existing(root)
            backend.unmount()
            phrase = confirmation('UPDATE', doc)
            show('Preserve installation / сохранить установку: ' + phrase)
            require(ask('Type exactly / введите точно: ') == phrase, 'Not confirmed; no write')
        root = backend.mount(doc, media, write=True)
        if action == 'wipe':
            backend.worker('prepare', doc, media, root, chosen)
        else:
            backend.worker('verify', doc, media, root, chosen)
            package.existing(root)
        path = package.stage(root, files, write=True)
        backend.unmount()
        root = backend.mount(doc, media)
        backend.worker('verify', doc, media, root, chosen)
        package.stage(root, files)
        return dict(status='pc-files-verified-activation-pending', action=action, target=chosen,
                    uuid=media['uuid'], media_id=doc['id'], package_path=path,
                    gateway_archive_sha256=package.ARCHIVE_SHA256, commissioned=False)
    finally:
        backend.close()



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true', help='inventory only; no mounts / только список')
    parser.add_argument('--report', type=Path, help='new public report on Ubuntu PC, outside CF')
    args = parser.parse_args()
    os.umask(0o077)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    try:
        require(os.geteuid() == 0, 'Run with sudo / запустите через sudo')
        require(all(shutil.which(tool) for tool in ['lsblk', 'blkid', 'e2fsck', 'mount', 'umount', 'sfdisk', 'mke2fs', 'udevadm', 'wipefs']),
                'Install Ubuntu packages: python3 util-linux fdisk e2fsprogs udev / установите зависимости')
        if not args.list:
            validate_installation(Path(__file__).resolve().parent)
        backend = Linux()
        disks = backend.scan()
        display_disks(disks)
        if args.list:
            return 0
        # This lock prevents two wizard instances, not other privileged programs.
        with open('/run/moxa-cf-wizard.lock', 'a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            selected = input('Card ID / ID карты: ').strip()
            matches = [d for d in disks if d['id'] == selected]
            require(len(matches) == 1, 'Unknown/ambiguous card / неверный выбор карты')
            doc = matches[0]
            chosen = target(input('Target name / имя прибора: ').strip(), input('Target IPv4:port / адрес: ').strip(),
                            input('Target LAN1 MAC (from trusted inventory) / MAC LAN1: ').strip())
            print('Target / прибор: ' + json.dumps(chosen))
            require(input('Confirm target: type MAC / подтвердите MAC: ').strip().lower() == chosen['mac'],
                    'Target not confirmed / прибор не подтверждён')
            action = input('1 ERASE ALL: first installation / первая установка; 2 UPDATE / обновление: ').strip()
            require(action in ('1', '2'), 'Unknown action / неверное действие')
            report = None
            if args.report:
                bootstrap.trusted_directory(args.report.absolute().parent)
                report = args.report.open('x', encoding='utf-8')
                report.write('{"status":"not-confirmed"}\n'); report.flush()
            with contextlib.ExitStack() as stack:
                if report:
                    stack.enter_context(report)
                result = execute(backend, doc, chosen, 'wipe' if action == '1' else 'update')
                if report:
                    report.seek(0); report.truncate()
                    json.dump(result, report, indent=2)
                    report.write('\n'); report.flush(); os.fsync(report.fileno())
            print(json.dumps(result, indent=2))
            print('Unmounted / Размонтировано. PC files verified; activation and commissioning on Moxa pending. See README.\n'
                  'Файлы проверены на PC; активация и ввод в эксплуатацию на Moxa ещё не выполнены. См. README.ru.md.')
        return 0
    except (Refused, ValueError) as error:
        print(str(error) if isinstance(error, Refused) else 'Validation failed / проверка не пройдена')
    except (OSError, subprocess.SubprocessError, KeyboardInterrupt, EOFError):
        print('Interrupted or operation failed / прервано или операция не подтверждена')
    print('STOP. Preserve all files; do not retry generation or repair automatically. Check mounts before removal.\n'
          'СТОП. Сохраните файлы; не повторяйте генерацию и не исправляйте автоматически. Проверьте mount перед извлечением.')
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
