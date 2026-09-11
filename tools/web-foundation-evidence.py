"""Read-only local evidence inspection; never connects to a device."""
import hashlib
import json
import re
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
result = {"archives": [], "preserved": []}
manifest = root / '_live_reference/uc7420-fw1.6/MANIFEST.md'
for line in manifest.read_text().splitlines():
    match = re.match(r'\| `([^`]+)` .* `([a-f0-9]{64})` \|', line)
    if match:
        name, expected = match.groups()
        digest = hashlib.sha256((manifest.parent / name).read_bytes()).hexdigest()
        assert digest == expected, name
        result['preserved'].append({'path': name, 'sha256': digest})
for p in (root / '_legacy_materials').rglob('*.tar.bz2'):
    if 'toolchain' in str(p):
        continue
    entry = {'path': p.relative_to(root).as_posix(), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
    try:
        with tarfile.open(p) as tar:
            names = [m.name for m in tar if 'keypad' in m.name.lower() or m.name.endswith('/random.c')]
        entry['relevant_members'] = names
    except (tarfile.TarError, OSError, EOFError) as error:
        entry['error'] = str(error)
    result['archives'].append(entry)
out = root / 'build/web-foundation/evidence.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
