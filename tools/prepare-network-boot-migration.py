"""Prepare an offline, reversible derivative of the preserved vendor script.

Never installs files or runs shell contents. The expected source SHA256 must
come from the freshly verified device baseline supplied by the operator.
"""
import argparse
import hashlib
import json
from pathlib import Path


def prepare(source: bytes) -> bytes:
    if len(source) > 16384 or b'\0' in source:
        raise ValueError('unsupported vendor script length/content')
    counts = {b'/sbin/ifup -a': 0, b'/sbin/ifdown -a': 0}
    result = []
    for line in source.splitlines(keepends=True):
        command = line.strip()
        if command in counts:
            counts[command] += 1
            direction = b'up' if command == b'/sbin/ifup -a' else b'down'
            line = line.replace(command, b'/etc/4vrs-network/gateway-network-recovery --network-vendor-' + direction + b' || exit $?')
        result.append(line)
    if any(count != 2 for count in counts.values()):
        raise ValueError('vendor ifup/ifdown call sites differ from the reviewed script')
    return b''.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vendor_script', type=Path)
    parser.add_argument('output_directory', type=Path)
    parser.add_argument('--expected-sha256', required=True)
    args = parser.parse_args()
    original = args.vendor_script.read_bytes()
    actual = hashlib.sha256(original).hexdigest()
    if actual != args.expected_sha256.lower():
        parser.error('preserved vendor script checksum mismatch')
    managed = prepare(original)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    target = args.output_directory / 'vendor-networking-managed'
    with target.open('xb') as stream:
        stream.write(managed)
    manifest = {'source_sha256': actual, 'managed_sha256': hashlib.sha256(managed).hexdigest(),
                'managed_size': len(managed), 'installed': False}
    with (args.output_directory / 'manifest.json').open('x', encoding='utf8') as stream:
        json.dump(manifest, stream, indent=2)
    print(json.dumps(manifest))


if __name__ == '__main__':
    main()
