"""Exercise the production C reader on the existing packager's exact output."""
import importlib.util
import io
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile

root, out, probe = map(Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location('package', root / 'tools/build-installer-package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def elf():
    b = bytearray(256)
    b[:7] = b'\x7fELF\x01\x02\x01'
    struct.pack_into('>HHI', b, 16, 2, 40, 1)
    struct.pack_into('>I', b, 28, 52)
    struct.pack_into('>IHHH', b, 36, 0x04000002, 52, 32, 2)
    struct.pack_into('>IIIIIIII', b, 52, 1, 0, 0, 0, 256, 256, 5, 4096)
    struct.pack_into('>IIIIIIII', b, 84, 3, 128, 0, 0, 19, 19, 4, 1)
    loader = b'/lib/ld-linux.so.3\0'
    struct.pack_into('>I', b, 100, len(loader))
    b[128:128+len(loader)] = loader
    b[160:174] = b'v2026.01.00\0\0\0'
    return bytes(b)


with tempfile.TemporaryDirectory(prefix='native-package-', dir=out) as tmp:
    tmp = Path(tmp)
    sources = []
    for i, name in enumerate(package.PAYLOAD_NAMES):
        path = tmp / name
        path.write_bytes(elf() if i < 2 else b'#!/bin/sh\nexit 0\n')
        sources.append(path)
    archive = tmp / 'package.tar.gz'
    package.package('v2026.01.00', *sources, archive)
    baseline = tmp / 'baseline'
    baseline.mkdir(mode=0o700)
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            path = baseline / Path(member.name).name
            path.write_bytes(tar.extractfile(member).read())
            path.chmod(member.mode)
    cases = 0

    def run(directory, success):
        global cases
        result = subprocess.run([str(probe), str(directory)], capture_output=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        cases += 1

    run(baseline, True)
    for name in package.PAYLOAD_NAMES + ('manifest.json', 'SHA256SUMS'):
        case = tmp / ('changed-' + name)
        shutil.copytree(baseline, case)
        target = case / name
        target.write_bytes(target.read_bytes() + b'X')
        run(case, False)
    for name in package.PAYLOAD_NAMES:
        case = tmp / ('link-' + name)
        shutil.copytree(baseline, case)
        target = case / name
        target.unlink()
        target.symlink_to(baseline / name)
        run(case, False)
    case = tmp / 'duplicate'
    shutil.copytree(baseline, case)
    target = case / 'manifest.json'
    target.write_bytes(target.read_bytes().replace(b'"format": 1,', b'"format": 1, "format": 1,'))
    run(case, False)
    case = tmp / 'mixed-versions'
    shutil.copytree(baseline, case)
    target = case / '4vrs-gateway'
    data = bytearray(target.read_bytes())
    data[190:203] = b'v2026.00.01\0\0'
    target.write_bytes(data)
    manifest_path = case / 'manifest.json'
    manifest = json.loads(manifest_path.read_bytes())
    manifest['files'][1]['size'] = len(data)
    manifest['files'][1]['sha256'] = hashlib.sha256(data).hexdigest()
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
    (case / 'SHA256SUMS').write_text(''.join(
        hashlib.sha256((case / name).read_bytes()).hexdigest() + '  ' + name + '\n'
        for name in sorted(package.PAYLOAD_NAMES + ('manifest.json',))))
    run(case, False)
    print(f'native package reader: {cases} cases passed (synthetic ELF, no target execution)')
