"""Unpack pinned dependency only into a new build directory (no downloads)."""
import hashlib
import sys
import tarfile
from pathlib import Path

archive, destination = map(Path, sys.argv[1:])
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
if digest != 'a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6':
    raise ValueError('TLS archive differs from the reviewed 3.6.7 release')
print('TLS archive SHA256', digest)
destination.mkdir(parents=True, exist_ok=False)
with tarfile.open(archive) as source:
    for member in source:
        target = (destination / member.name).resolve()
        if destination.resolve() not in target.parents or not (member.isfile() or member.isdir()):
            raise ValueError('Unsafe archive member: ' + member.name)
        source.extract(member, destination)
