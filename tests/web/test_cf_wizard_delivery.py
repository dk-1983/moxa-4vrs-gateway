"""Run with only this driver and ZIP mounted, no repository, network or devices."""
from pathlib import Path
import hashlib,importlib.util,json,os,subprocess,sys,tempfile,zipfile
archive=Path(sys.argv[1]);expected={'4vrs-cf-rng-worker','cf-wizard.py','cf-bootstrap.py','cf-package.py','gateway.tar.gz','README.md','README.ru.md','LICENSE','LICENSE.mbedtls','NOTICE','source/cf_rng_writer.c','manifest.json','SHA256SUMS'}
with tempfile.TemporaryDirectory(prefix='wizard-kit-',dir='/run') as d:
 d=Path(d)
 with zipfile.ZipFile(archive) as z:
  assert set(z.namelist())==expected and z.testzip() is None
  for name in z.namelist():
   p=d/name;p.parent.mkdir(exist_ok=True);p.write_bytes(z.read(name));p.chmod(z.getinfo(name).external_attr>>16&0o777)
 for line in (d/'SHA256SUMS').read_text().splitlines():
  digest,name=line.split('  ');assert hashlib.sha256((d/name).read_bytes()).hexdigest()==digest
 manifest=json.loads((d/'manifest.json').read_text())
 for name,item in manifest['files'].items():
  p=d/name;assert p.stat().st_size==item['bytes'] and p.stat().st_mode&0o777==int(item['mode'],8)
 spec=importlib.util.spec_from_file_location('wizard',d/'cf-wizard.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
 w.validate_installation(d)
 w.package.load(d/'gateway.tar.gz')
 p=d/'4vrs-cf-rng-worker';original=p.read_bytes();p.write_bytes(b'bad worker')
 try:w.validate_installation(d)
 except w.Refused:pass
 else:raise AssertionError('tampered worker accepted')
 p.write_bytes(original)
 r=subprocess.run([sys.executable,d/'cf-wizard.py','--help'],capture_output=True);assert r.returncode==0 and b'--report' in r.stdout
 r=subprocess.run([sys.executable,d/'cf-wizard.py','--image','/no-such-image'],capture_output=True);assert r.returncode==2
 r=subprocess.run([d/'4vrs-cf-rng-worker'],capture_output=True);assert r.returncode==2
 r=subprocess.run(['ldd',d/'4vrs-cf-rng-worker'],capture_output=True);assert r.returncode==0 and b'not found' not in r.stdout and b'ubsan' not in r.stdout
 assert all(name not in expected for name in ['state','pending','seed','trial-policy'])
 print('Standalone Ubuntu kit: allowlist, checksums, modes, runtime dependencies, trusted kit, tamper refusal, help and no-image CLI PASS')
