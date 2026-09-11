"""Run with only the delivered ZIP mounted; no repository/runtime dependencies."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import zipfile

archive = Path(sys.argv[1])
kit = Path('/root/cf-kit-test')
kit.mkdir()
with zipfile.ZipFile(archive) as z:
    for info in z.infolist():
        assert not info.filename.startswith('/') and '..' not in Path(info.filename).parts
        path = kit/info.filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(z.read(info))
        path.chmod((info.external_attr >> 16) & 0o777)
manifest = json.loads((kit/'manifest.json').read_text())
assert not manifest['contains_seed']
for name, record in manifest['files'].items():
    data = (kit/name).read_bytes()
    assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256']
subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=kit, check=True)
result = subprocess.run(['python3', kit/'cf-bootstrap.py', '--help'], capture_output=True, check=True)
assert all(word in result.stdout for word in [b'inspect', b'prepare', b'verify'])
mount = Path('/root/cf-public-mount'); mount.mkdir()
disk = Path('/root/public-disk.img'); disk.write_bytes(b'PUBLIC')
partition = Path('/root/public-partition.img'); partition.write_bytes(b'PUBLIC')
args = ['--disk', disk, '--partition', partition, '--mount', mount,
        '--target-confirmation', kit/'target-confirmation.example.json', '--receipt', kit/'receipt.json']
for mode in ['inspect', 'verify', 'prepare']:
    result = subprocess.run(['python3', kit/'cf-bootstrap.py', mode, *args], capture_output=True, timeout=5)
    assert result.returncode == 1
    assert not (mount/'4vrs-rng').exists() and not (kit/'receipt.json').exists()
    if mode == 'prepare': assert b'explicit --authorize-write required' in result.stderr
    else: assert b'physical block device required' in result.stderr
assert subprocess.run([kit/'4vrs-cf-rng-worker'], capture_output=True).returncode == 2
print('PASS portable delivery: exact manifest/SHA256SUMS, executable mode, help, three fail-closed CLI modes; no repository or production seed')
