"""Offline preparation only. Never opens COM, connects to a target or signals init."""
import argparse
import hashlib
import json
from pathlib import Path

ORIGINAL = b's1:2345:respawn:/sbin/getty 115200 ttyS1 -L'
HELD = b's1:2345:respawn:/bin/sh /var/hda/4vrs-bootstrap/hold-ttyS1.sh'
VENDOR_INIT_MD5 = '6471201dbdbf3f4e029801d499bd8ff7'
VENDOR_INIT_SHA256 = '6b386c7c88410774d57704fdf52bb9a1dd3feab321382ff97bf0d657143000b1'


def transform(data, restore=False):
    if b'\r' in data or b'\0' in data or not data.endswith(b'\n'):
        raise ValueError('requires unchanged LF-terminated target inittab')
    lines = data.splitlines(keepends=True)
    candidates = [i for i, line in enumerate(lines) if line.lstrip().startswith(b's1:')]
    old, new = (HELD, ORIGINAL) if restore else (ORIGINAL, HELD)
    if len(candidates) != 1 or lines[candidates[0]] != old + b'\n':
        raise ValueError('s1 differs from the reviewed entry; refuse')
    lines[candidates[0]] = new + b'\n'
    return b''.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inittab', type=Path)
    parser.add_argument('new_directory', type=Path)
    args = parser.parse_args()
    original = args.inittab.read_bytes(); held = transform(original)
    assert transform(held, restore=True) == original
    args.new_directory.mkdir()  # Never overwrite an earlier preparation.
    for name, data in [('inittab.original', original), ('inittab.held', held)]:
        (args.new_directory/name).write_bytes(data)
    manifest = {
        'active_init_verified': False,
        'alias_ownership_proven': False,
        'physical_probe_passed': False,
        'vendor_init_md5': VENDOR_INIT_MD5,
        'vendor_init_sha256': VENDOR_INIT_SHA256,
        'required_change': 'Only s1 process changes; retain id, runlevels and respawn action',
        'files': {name:{'sha256':hashlib.sha256(data).hexdigest(),'md5':hashlib.md5(data).hexdigest()}
                  for name,data in [('inittab.original', original), ('inittab.held', held)]}
    }
    (args.new_directory/'plan.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('Offline plan created. Live init identity and channel ownership are still required.')


if __name__ == '__main__':
    main()
