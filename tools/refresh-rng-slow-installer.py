"""Refresh only changed installer source plus noncompiled review inputs."""
from pathlib import Path
import hashlib,json,shutil,subprocess
host=Path('/workspace/source-host');base=Path('/workspace/build/rng-slow-commit-v1-20260911');snapshot=base/'source'
manifest=json.loads((base/'source-manifest.json').read_text())
for n,h in manifest.items():
    if n.startswith('src/') and n!='src/installer/install_orchestrator.c':assert hashlib.sha256((host/n).read_bytes()).hexdigest()==h,n
paths=[host/'src/installer/install_orchestrator.c']
for dirname in ['tools','tests']:
    paths += [p for p in (host/dirname).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
for p in paths:
    n=p.relative_to(host).as_posix();dest=snapshot/n;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest);manifest[n]=hashlib.sha256(p.read_bytes()).hexdigest()
(base/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
with (base/'build-installer-schema3.log').open('wb') as log:
    subprocess.run(['python3',snapshot/'tools/build-installer.py',snapshot,base/'target/4vrs-install'],stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run(['python3',host/'tools/package-rng-slow-commit.py','installer-check'],check=True)
