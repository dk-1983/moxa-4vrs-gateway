#!/usr/bin/env python3
"""Prepare local, reversible guards for captured Moxa vendor time writers.

Does not connect to a device or execute vendor scripts. Input files must be
fresh read-only captures named vendor-ntpdate, vendor-ntpdate.d and vendor-halt.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

MARKER = '/etc/4vrs-clock-managed'


def prepare(source, destination):
    destination.mkdir(parents=True, exist_ok=False)
    manifest = {}
    patches = []
    for name in ('ntpdate', 'ntpdate.d', 'halt'):
        original = (source / ('vendor-' + name)).read_bytes()
        text = original.decode('ascii')
        if '\r' in text or MARKER in text:
            raise ValueError('Unexpected line endings or existing guard: ' + name)
        if name == 'halt':
            old = 'hwclock --systohc\n'
            new = ('# Gateway saves RTC only after trusted synchronization.\n'
                   'if test ! -e ' + MARKER + '; then\n'
                   '  hwclock --systohc\nfi\n')
        else:
            old = 'start)\n'
            new = ('start)\n'
                   '  # Gateway owns time policy while this marker exists.\n'
                   '  test ! -e ' + MARKER + ' || exit 0\n')
        if text.count(old) != 1:
            raise ValueError('Unexpected vendor script: ' + name)
        updated = text.replace(old, new).encode('ascii')
        (destination / name).write_bytes(updated)
        manifest[name] = {'before_md5': hashlib.md5(original).hexdigest(),
                          'after_md5': hashlib.md5(updated).hexdigest()}
        patches.extend(difflib.unified_diff(text.splitlines(True),
                       updated.decode('ascii').splitlines(True),
                       fromfile='/etc/init.d/' + name,
                       tofile='/etc/init.d/' + name))
    (destination / 'migration.patch').write_text(''.join(patches), encoding='ascii')
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.captures, args.output)
