"""Offline, explicit one-field migration; never opens a device or edits input.

Usage: python tools/prepare-moxa2-dns-copy.py ARCHIVE NEW_OUTPUT_DIRECTORY
The directory must not exist. Review its diff before any separate installation.
"""
import hashlib
import ipaddress
import json
import pathlib
import re
import sys


def normalize(document, resolver):
    """Only one eth0 dns-servers row, with the same ordered resolver literals."""
    if b'\x00' in document + resolver:
        raise ValueError('dns-copy: embedded-nul')
    lines = document.splitlines(keepends=True)
    owner = None
    target = None
    values = None
    for index, line in enumerate(lines):
        fields = line.split()
        if not fields or fields[0].startswith(b'#'):
            continue
        if fields[0] == b'iface':
            owner = fields[1:] if len(fields) == 4 else None
        elif fields[0] in (b'auto', b'allow-hotplug'):
            owner = None
        elif fields[0].startswith(b'dns-'):
            if fields[0] != b'dns-servers' or target is not None or owner != [b'eth0', b'inet', b'static']:
                raise ValueError('dns-copy: ambiguous-directives')
            target, values = index, fields[1:]
    servers = [line.split()[1:] for line in resolver.splitlines()
               if line.split() and line.split()[0] == b'nameserver']
    if target is None or not 1 <= len(values) <= 2 or any(len(row) != 1 for row in servers):
        raise ValueError('dns-copy: invalid-count')
    servers = [row[0] for row in servers]
    if values != servers or len(set(values)) != len(values):
        raise ValueError('dns-copy: resolver-conflict-or-duplicate')
    for value in values:
        ip = ipaddress.IPv4Address(value.decode('ascii'))
        if not int(ip) or ip.is_loopback or ip.is_multicast or int(ip) >> 24 >= 224:
            raise ValueError('dns-copy: invalid-ipv4')
    lines[target], count = re.subn(rb'^(\s*)dns-servers(?=\s)', rb'\1dns-nameservers', lines[target], count=1)
    if count != 1:
        raise ValueError('dns-copy: unexpected-row')
    return b''.join(lines)


def digest(data):
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), md5=hashlib.md5(data).hexdigest())


def main():
    archive, output = map(pathlib.Path, sys.argv[1:])
    manifest = json.loads((archive / 'manifest.json').read_text())
    files = {}
    for record in manifest.values():
        name = record['file']
        if pathlib.Path(name).name != name or '/' in name or '\\' in name:
            raise ValueError('archive: invalid-name')
        data = (archive / name).read_bytes()
        if any(digest(data)[key] != record[key] for key in ('bytes', 'sha256', 'md5')):
            raise ValueError('archive: hash-mismatch')
        files[name] = data
    key = 'etc__network__interfaces'
    normalized = normalize(files[key], files['etc__resolv.conf'])
    output.mkdir()  # No reuse, overwrite, or mutation of archive files.
    for name in (key, 'etc__resolv.conf', 'var__hda__4vrs__config__gateway.conf'):
        (output / name).write_bytes(normalized if name == key else files[name])
    proof = {'baseline': manifest, 'before': digest(files[key]), 'after': digest(normalized),
             'change': 'one eth0 key dns-servers -> dns-nameservers; values and all other bytes retained'}
    (output / 'migration.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({'stage': 'copy-ready', 'verified_files': len(files), 'before': proof['before'], 'after': proof['after']}))


if __name__ == '__main__':
    main()
