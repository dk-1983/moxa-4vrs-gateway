"""Reuse hardware-qualified runtime; rebuild only installer diagnostics."""
from pathlib import Path
import shutil,json,hashlib,subprocess,os,sys
root=Path('/workspace/source-host');base=Path('/workspace/build/release-final-20260911');base.mkdir(exist_ok=True)
old=Path('/workspace/build/web-ipc-v1-20260911');source=base/'source'
for name in ['src','tools','tests','deploy','assets/icons','assets/help']:shutil.copytree(root/name,source/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
previous=json.loads((old/'source-manifest.json').read_text())
assert all(hashlib.sha256((root/n).read_bytes()).hexdigest()==h for n,h in previous.items() if n.startswith('src/') and not n.startswith('src/installer/'))
(base/'source-manifest.json').write_text(json.dumps({p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'src').rglob('*') if p.is_file()},indent=2)+'\n')
for name in ['target','cf-wizard']:shutil.copytree(old/name,base/name,dirs_exist_ok=True)
subprocess.run(['python3',root/'tools/build-installer.py',source,base/'target/4vrs-install'],check=True,stdout=(base/'build-installer.log').open('w'))
for mode in ([] if 'refresh' in sys.argv[1:] else ['host','ubsan']):
    env=os.environ.copy()
    if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
    with (base/('installer-'+mode+'.log')).open('w') as log:subprocess.run(['sh',source/'tools/run-host-installer-tests.sh',source,base/('installer-'+mode)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    print('PASS installer',mode,flush=True)
