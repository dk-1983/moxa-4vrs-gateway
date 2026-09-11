"""Negative public-main boundaries and read-only reconnect result in chroot."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

source, out, package = map(Path, sys.argv[1:])
env = dict(os.environ, CROSS_CC='cc',
           CFLAGS=os.environ.get('CFLAGS', '-std=c99 -O2 -g -Wall -Wextra -Werror'))
binary = out / '4vrs-install-host-entry'
subprocess.run([sys.executable, str(source / 'tools/build-installer.py'), str(source), str(binary)], env=env, check=True)
for arguments, cwd, code in [(['--root', '/tmp'], out, 2), ([], package, 1)]:
    result = subprocess.run([str(binary), *arguments], cwd=cwd, capture_output=True, timeout=5)
    assert result.returncode == code, result.stderr
    print('public main:', arguments, result.returncode, result.stderr.decode().strip())
root = Path(tempfile.mkdtemp(prefix='entry-root-', dir=out))
(root / 'etc/4vrs-installer').mkdir(parents=True)
shutil.copyfile(binary, root / 'install'); (root / 'install').chmod(0o755)
for name in re.findall(r'(/[^\s()]+)', subprocess.check_output(['ldd', str(binary)], text=True)):
    if Path(name).is_file():
        dest = root / name.lstrip('/'); dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(name, dest); dest.chmod(0o755)
record = root / 'etc/4vrs-installer/result'
expected = b'version=v2026.01.01\nresult=3\nstage=already-installed\n'
record.write_bytes(expected); record.chmod(0o600)
result = subprocess.run(['chroot', str(root), '/install', '--status'], capture_output=True, timeout=5)
assert result.returncode == 0 and result.stdout == expected
assert record.read_bytes() == expected
record.write_bytes(b'x' * 1025)
result = subprocess.run(['chroot', str(root), '/install', '--status'], capture_output=True, timeout=5)
assert result.returncode == 1 and not result.stdout
print('public main: arbitrary root refused, foreign self refused, bounded read-only status passed')
import hashlib,struct,fcntl
expected=b'result=0\n'+b'diagnostic padding\n'*35
assert 512<len(expected)<1024
record.write_bytes(expected)
result=subprocess.run(['chroot',str(root),'/install','--status'],capture_output=True,timeout=5)
assert result.returncode==0 and result.stdout==expected
lock=root/'etc/4vrs-installer/install.lock';lock.write_bytes(b'');lock.chmod(0o600)
slot=root/'etc/4vrs-installer/slot0';slot.mkdir(mode=0o700)
path=b'etc/4vrs-clock-managed';secret=b'private-test-sentinel-do-not-print'
def journal(mode):
    data=b'4VIJ0001'+struct.pack('>II',1,1)+struct.pack('>II',len(path),0)+path
    for m in [0o600,mode]:data+=struct.pack('>III',1,m,len(secret))+secret
    return data+hashlib.sha256(data).hexdigest().encode()
j=slot/'journal';j.write_bytes(journal(0o644));j.chmod(0o600)
before=j.read_bytes();result=subprocess.run(['chroot',str(root),'/install','--decision-journal'],capture_output=True,timeout=5)
assert result.returncode==0 and b'before_mode=600 after_mode=644' in result.stdout and b'content_changed=0' in result.stdout and secret not in result.stdout and before==j.read_bytes()
with lock.open('rb') as f:
    fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
    assert subprocess.run(['chroot',str(root),'/install','--decision-journal'],capture_output=True).returncode==1
j.write_bytes(before[:-1]+b'X')
assert subprocess.run(['chroot',str(root),'/install','--decision-journal'],capture_output=True).returncode==1
print('public main: extended read-only status, journal metadata without contents, concurrent-lock refusal and corrupt journal refusal passed')
