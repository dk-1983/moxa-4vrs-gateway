"""Extract only investigation text copies to build; archives stay immutable."""
import hashlib
import json
import tarfile
from pathlib import Path
root = Path(__file__).resolve().parents[1]
archive = root / '_legacy_materials/CD/src/UC-7402.7408.7410.7420-LX Plus/UC-7400P_10021009.tar.bz2'
out = root / 'build/web-foundation/keypad'
out.mkdir(parents=True, exist_ok=True)
found = []
with tarfile.open(archive) as tar:
    for member in tar:
        if not member.isfile() or member.size > 300000 or not member.name.endswith(('.c', '.h')):
            continue
        if '/kernel/' not in member.name and '/host/moxalib/' not in member.name:
            continue
        data = tar.extractfile(member).read()
        if b'IOCTL_KEYPAD' in data or b'KEYPAD_HAS_PRESS' in data or member.name.endswith('/drivers/char/random.c'):
            name = str(len(found)) + '-' + Path(member.name).name
            (out / name).write_bytes(data)
            found.append({'member': member.name, 'copy': name, 'sha256': hashlib.sha256(data).hexdigest()})
(out / 'index.json').write_text(json.dumps(found, indent=2))
print(json.dumps(found, indent=2))
