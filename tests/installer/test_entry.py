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
expected = b'version=v2026.01.00\nresult=3\nstage=already-installed\n'
record.write_bytes(expected); record.chmod(0o600)
result = subprocess.run(['chroot', str(root), '/install', '--status'], capture_output=True, timeout=5)
assert result.returncode == 0 and result.stdout == expected
assert record.read_bytes() == expected
record.write_bytes(b'x' * 513)
result = subprocess.run(['chroot', str(root), '/install', '--status'], capture_output=True, timeout=5)
assert result.returncode == 1 and not result.stdout
print('public main: arbitrary root refused, foreign self refused, bounded read-only status passed')
