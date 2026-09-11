"""Allowlisted, seed-free portable Ubuntu x86-64 kit. Does not prepare any CF."""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys
import zipfile

root, binaries, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
sha = lambda data: hashlib.sha256(data).hexdigest()
worker = (binaries/'4vrs-cf-rng-worker').read_bytes()
if worker[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', worker, 18)[0] != 62:
    raise SystemExit('Ubuntu x86-64 ELF worker required')
for forbidden in [b'CF_FIXTURE', b'CF_RANDOM', b'CF_FAIL', b'CF_CRASH', b'__wrap_getrandom', b'fixture-device-A']:
    if forbidden in worker:
        raise SystemExit('fixture marker in production worker')
if b'getrandom' not in worker:
    raise SystemExit('missing OS CSPRNG import')

def instructions(name):
    text = (root/'docs'/name).read_text(encoding='utf-8')
    def link(match):
        label, target = match.groups()
        if target == 'cf-bootstrap.md': return '['+label+'](README.md)'
        if target == 'cf-bootstrap.ru.md': return '['+label+'](README.ru.md)'
        # The offline kit is deliberately independent of the complete docs tree.
        return label+' (`docs/'+target+'` in repository)'
    return re.sub(r'\[([^\]]+)\]\(([^)]+)\)', link, text).encode('utf-8')

items = {
    '4vrs-cf-rng-worker': worker,
    'cf-bootstrap.py': (root/'tools/cf-bootstrap.py').read_bytes(),
    'README.md': instructions('cf-bootstrap.md'),
    'README.ru.md': instructions('cf-bootstrap.ru.md'),
    'LICENSE': (root/'LICENSE').read_bytes(),
    'LICENSE.mbedtls': (binaries/'MBEDTLS-LICENSE').read_bytes(),
    'NOTICE': b'4VRS CF bootstrap local service candidate 2026-09-10.\n'
              b'Includes unmodified Mbed TLS 3.6.7 SHA256/platform code, Copyright The Mbed TLS Contributors.\n'
              b'Apache-2.0 option selected; see LICENSE.mbedtls. No vendor SDK or toolchain included.\n',
    'target-confirmation.example.json': (json.dumps({
        'confirmed': False, 'address': '10.0.2.13:622', 'mac': '00:90:e8:1f:4c:f1',
        'cf_uuid': 'REPLACE_WITH_FRESHLY_CONFIRMED_MOXA1_CF_UUID',
        'disk_bytes': 4009549824, 'partition_bytes': 4008501248}, indent=2)+'\n').encode(),
    'source/cf_rng_writer.c': (root/'src/host/cf_rng_writer.c').read_bytes(),
}
public_fixture = bytes(255 if i == 5 else i for i in range(32))
for name, data in items.items():
    if public_fixture in data or any(part in {'state', 'pending', 'seed', 'trial-policy'} for part in Path(name).parts):
        raise SystemExit('state/test seed in delivery')
manifest = {'format': 1, 'scope': 'Moxa #1 only; Ubuntu 24.04 x86-64; local qualification, physical write pending',
            'contains_seed': False, 'files': {k: {'bytes': len(v), 'sha256': sha(v)} for k, v in sorted(items.items())}}
items['manifest.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
items['SHA256SUMS'] = ''.join(sha(v)+'  '+k+'\n' for k, v in sorted(items.items())).encode()
archive = output/'4vrs-cf-bootstrap-moxa1-20260910.zip'
with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
    for name, data in sorted(items.items()):
        info = zipfile.ZipInfo(name, (2026, 9, 10, 0, 0, 0))
        info.create_system = 3
        info.external_attr = (0o100755 if name == '4vrs-cf-rng-worker' else 0o100644) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None and set(z.namelist()) == set(items)
    assert all(z.read(name) == data for name, data in items.items())
result = {'archive': archive.name, 'bytes': archive.stat().st_size, 'sha256': sha(archive.read_bytes()),
          'entries': len(items), 'worker_sha256': sha(worker), 'allowlist_audit': 'pass',
          'production_seed_generated': False}
(output/'delivery.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
(output/'SHA256SUMS').write_text(result['sha256']+'  '+archive.name+'\n', encoding='ascii')
print(json.dumps(result, indent=2))
