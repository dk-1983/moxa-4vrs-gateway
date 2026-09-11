"""Execute the generated gates and real deployment scripts in a private chroot.

Only the qualification executable and harmless logging hardware stubs exist
inside it. No networking, shutdown, UART or service command can reach the host.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

source, out = map(Path, sys.argv[1:])
root = Path((out / 'shell-root.txt').read_text().strip())
assert root.is_dir() and root.parent == out and root.name.startswith('orchestration-')

def copy_binary(binary, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(binary, destination)
    destination.chmod(0o755)
    dependencies = subprocess.check_output(['ldd', str(binary)], text=True)
    for name in re.findall(r'(/[^\s()]+)', dependencies):
        path = Path(name)
        if path.is_file():
            dest = root / name.lstrip('/')
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
            dest.chmod(0o755)

copy_binary(Path('/bin/dash'), root / 'bin/sh')
copy_binary(out / 'test-orchestrator', root / 'etc/4vrs-installer/recovery')
shutil.copyfile(source / 'deploy/4vrs-networking-wrapper', root / 'etc/4vrs-installer/networking')
(root / 'etc/4vrs-installer/networking').chmod(0o755)

def stub(path, label):
    dest = root / path.lstrip('/')
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text('#!/bin/sh\nprintf "%s %s\\n" "' + label + '" "$1" >> /events\n')
    dest.chmod(0o755)

stub('/etc/4vrs-network/gateway-network-recovery', 'helper')
stub('/sbin/ntpdate', 'FORBIDDEN-ntpdate')
stub('/usr/sbin/ntpdate', 'FORBIDDEN-ntpdate')
stub('/bin/hwclock', 'FORBIDDEN-hwclock')
stub('/sbin/halt', 'fake-halt')

def run(path, *args, success=True):
    command = ['chroot', str(root), '/bin/sh', path, *args]
    result = subprocess.run(command, capture_output=True, text=True, timeout=15)
    print(command, 'exit=' + str(result.returncode), result.stderr, flush=True)
    assert (result.returncode == 0) == success

def events():
    return (root / 'events').read_text().splitlines()

run('/etc/rc.d/rcS.d/S40networking', 'start')
run('/etc/init.d/4vrs-gateway', 'start')
assert events() == ['helper --network-boot', 'helper --network-vendor-up',
                    'helper --network-owner-start', 'application start']
run('/etc/rc.d/rcS.d/S40networking', 'restart')
assert events()[-4:] == ['helper --network-boot', 'helper --network-vendor-down',
                         'helper --network-vendor-up', 'helper --network-owner-start']
run('/etc/init.d/4vrs-gateway', 'stop')
run('/etc/rc.d/rcS.d/S40networking', 'start')
run('/etc/init.d/4vrs-gateway', 'start')
run('/etc/init.d/4vrs-gateway', 'status')
assert events()[-6:] == ['application stop', 'helper --network-boot',
                         'helper --network-vendor-up', 'helper --network-owner-start',
                         'application start', 'application status']
run('/etc/init.d/ntpdate', 'start')
run('/etc/init.d/halt')
assert not any('FORBIDDEN' in row for row in events())
assert events()[-1] == 'fake-halt '
# Historical r15 guards: shell behavior for the supported regular marker.
(root / 'etc/init.d/ntpdate').write_text('#! /bin/sh\ntest -f /etc/4vrs-clock-managed && exit 0\n/usr/sbin/ntpdate own-server\n')
(root / 'etc/init.d/halt').write_text('#! /bin/sh\nif ! test -f /etc/4vrs-clock-managed; then\n    hwclock --systohc\nfi\nhalt\n')
run('/etc/init.d/ntpdate', 'start')
run('/etc/init.d/halt')
assert not any('FORBIDDEN' in row for row in events())
(root / 'etc/4vrs-clock-managed').unlink()
run('/etc/init.d/ntpdate', 'start')
run('/etc/init.d/halt')
assert 'FORBIDDEN-ntpdate own-server' in events()
assert 'FORBIDDEN-hwclock --systohc' in events()
(root / 'etc/4vrs-clock-managed').write_text('fixture restored\n')
(root / 'missing-cf').touch()
before = events()
run('/etc/init.d/4vrs-gateway', 'start', success=False)
assert events() == before
(root / 'missing-cf').unlink()
run('/etc/init.d/4vrs-gateway', 'start')
assert events()[-1] == 'application start'
active = (root / 'etc/4vrs-installer/active').read_text()
journal = root / 'etc/4vrs-installer' / active / 'journal'
data = bytearray(journal.read_bytes()); data[20] ^= 1; journal.write_bytes(data)
before = events()
# rcS may ignore the S40 error and continue. The application gate STILL blocks.
run('/etc/rc.d/rcS.d/S40networking', 'start', success=False)
run('/etc/init.d/4vrs-gateway', 'start', success=False)
assert events() == before
print('production boot gates/scripts: 17 bounded chroot invocations passed; hardware stubs only')
