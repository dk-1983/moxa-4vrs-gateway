"""Compile against the same explicit source set as the full Gateway."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import hashlib
import json

root, out, evidence, package, observation = map(Path, sys.argv[1:])
# Windows evidence mounts expose 0777 regardless of captured target mode.
# Keep original bytes immutable; model private target metadata on copies.
copy = Path(tempfile.mkdtemp(prefix='archive-input-', dir=out))
manifest = json.loads((evidence / 'manifest.json').read_text())
for record in manifest.values():
    data = (evidence / record['file']).read_bytes()
    assert len(data) == record['bytes']
    assert hashlib.sha256(data).hexdigest() == record['sha256']
    assert hashlib.md5(data).hexdigest() == record['md5']
    target = copy / record['file']; target.write_bytes(data); target.chmod(0o600)
data = (evidence / 'etc__init.d__halt').read_bytes()
target = copy / 'etc__init.d__halt'; target.write_bytes(data); target.chmod(0o600)
print('archive manifest verified; supplementary halt SHA256=' + hashlib.sha256(data).hexdigest(), flush=True)
sources = re.findall(r'"\$root/(src/[^"\s]+\.c)"', (root / 'tools/build-gateway-application.sh').read_text())
assert sources and len(sources) == len(set(sources)) and sources[0] == 'src/app/main.c'
command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('CFLAGS', '-std=c99 -O2 -g -Wall -Wextra -Werror')),
           '-I' + str(root / 'src'), str(root / 'tests/installer/archive_probe.c'),
           *[str(path) for path in sorted((root / 'src/installer').glob('*.c')) if path.name != 'main.c'],
           *[str(root / name) for name in sources[1:]], '-lrt', '-Wl,--wrap=fsync', '-o', str(out / 'archive-probe')]
print(shlex.join(command), flush=True)
subprocess.run(command, check=True)
subprocess.run([str(out / 'archive-probe'), str(copy), str(out), str(package), str(observation)], check=True)
