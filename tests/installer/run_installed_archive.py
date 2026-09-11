"""Verified installed trial archive, qualification aliases only; no live root."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root, out, evidence, package = map(Path, sys.argv[1:])
records = json.loads((evidence / 'evidence-manifest.json').read_text())
for name, record in records.items():
    assert Path(name).name == name
    data = (evidence / name).read_bytes()
    assert len(data) == record['bytes']
    assert hashlib.sha256(data).hexdigest() == record['sha256']
copy = Path(tempfile.mkdtemp(dir=out, prefix='installed-input-'))
for name in ('baseline.tar', 'after-refusal.tar'):
    shutil.copyfile(evidence / 'installed.tar', copy / name)
(copy / 'baseline-files.json').write_bytes((evidence / 'installed-files.json').read_bytes())
(copy / 'baseline-manifest.json').write_text(json.dumps(records['installed.tar']))
print('Verified manifest; qualification aliases contain unchanged installed.tar bytes', flush=True)
subprocess.run([sys.executable, str(root / 'tests/installer/run_managed_archive.py'),
                str(root), str(out), str(copy), str(package), 'complete'], check=True)
