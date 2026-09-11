"""Offline integrated qualification. Run in a private Linux build container.

/evidence/moxa{1,2}-v2026-01-01 must be read-only preserved captures.
Never executes target ELF or contacts a device.
"""
import hashlib, json, os, subprocess, sys, tarfile
from pathlib import Path
root=Path('/workspace/source');host=Path('/workspace/source-host')
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False);records=[]
base=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
def run(name,args,env=None):
 print('START',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  result=subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,env=env or base)
 records.append({'name':name,'command':list(map(str,args)),'exit':result.returncode})
 (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
 print(name,'PASS' if not result.returncode else 'FAIL',flush=True)
 if result.returncode:print((out/(name+'.log')).read_text()[-6000:],flush=True);raise SystemExit(result.returncode)
run('web',['python3',root/'tools/qualify-web-integration.py',out/'web'])
run('installer-target',['python3',root/'tools/build-installer.py',root,out/'4vrs-install'])
for mode in ('host','ubsan'):
 env=base.copy()
 if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
 run('installer-'+mode,['sh',root/'tools/run-host-installer-tests.sh',root,out/('installer-'+mode)],env)
 run('apache-'+mode,['python3',root/'tests/installer/run_apache_runtime.py',root,out/('apache-'+mode)],env)
 run('cost-probe-'+mode,[out/'web'/mode/'4vrs-web-cost-probe','--measure'])
for name in ('test_package.py','test_web_package_contract.py'):
 run(name,['python3',root/'tests/installer'/name])
run('keypad-probe-build',['/usr/local/xscale_be/bin/xscale_be-gcc','-std=c99','-Os','-Wall','-Wextra','-Werror','-mcpu=xscale','-mbig-endian','-msoft-float',root/'tools/keypad-event-probe.c','-lrt','-o',out/'4vrs-keypad-event-probe'])
run('all-abi',['python3',root/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'all-abi.json',out/'4vrs-install',out/'web/target/4vrs-gateway',out/'web/target/4vrs-web',out/'web/target/4vrs-web-cost-probe',out/'web/target/4vrs-rng',out/'web/target/4vrs-kdf',out/'4vrs-keypad-event-probe'])
archive=out/'4vrs-gateway-v2026.02.01-console-candidate.tar.gz'
args=['python3',root/'tools/build-installer-package.py','--version','v2026.02.01','--installer',out/'4vrs-install','--gateway',out/'web/target/4vrs-gateway','--web',out/'web/target/4vrs-web','--rng',out/'web/target/4vrs-rng','--kdf',out/'web/target/4vrs-kdf','--license-file',out/'web/dependency/mbedtls-3.6.7/LICENSE','--init-script',root/'deploy/4vrs-gateway.init','--wrapper',root/'deploy/4vrs-networking-wrapper','--output',archive]
run('package',args);run('package-repeat',args[:-1]+[out/'repeat.tar.gz'])
assert archive.read_bytes()==(out/'repeat.tar.gz').read_bytes()
package=out/'4vrs-gateway-v2026.02.01';package.mkdir()
with tarfile.open(archive) as tar:
 for member in tar:
  assert member.isfile() and member.name.startswith(package.name+'/')
  name=member.name[len(package.name)+1:];assert name and '/' not in name and name not in ('.','..')
  path=package/name;path.write_bytes(tar.extractfile(member).read());path.chmod(member.mode)
run('native-package',[out/'installer-host/package-probe',package])
run('public-entry',['python3',root/'tests/installer/test_entry.py',root,out,package])
for unit in (1,2):
 for mode in ('host','ubsan'):
  env=base.copy()
  if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
  run(f'archive-moxa{unit}-{mode}',['python3',root/'tests/installer/run_release_archive.py',root,out/f'archive-moxa{unit}-{mode}',Path('/evidence')/f'moxa{unit}-v2026-01-01',package,'complete'],env)
files=[archive,out/'4vrs-install',out/'4vrs-keypad-event-probe',out/'web/target/4vrs-gateway',out/'web/target/4vrs-web',out/'web/target/4vrs-web-cost-probe',out/'web/target/4vrs-rng',out/'web/target/4vrs-kdf',*sorted(package.iterdir())]
manifest={str(p.relative_to(out)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'md5':hashlib.md5(p.read_bytes()).hexdigest()} for p in files}
(out/'artifact-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for name,digest in json.loads((out/'web/source-manifest.json').read_text()).items():assert hashlib.sha256((host/name).read_bytes()).hexdigest()==digest,name
print('COMPLETE: native offline install/recovery; no target execution or hardware access.',flush=True)
