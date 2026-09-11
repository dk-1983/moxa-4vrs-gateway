"""Offline Ubuntu image/worker regression and evidence export, never production RNG."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
root,binaries,legacy,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
evidence={'scope':'public file images; injected mount boundary; no physical CF, loop mounts, Moxa or production seeds',
          'os':Path('/etc/os-release').read_text(),'tests':{}}
def run(name,args):
 p=subprocess.run(list(map(str,args)),capture_output=True);(out/(name+'.log')).write_bytes(p.stdout+p.stderr)
 evidence['tests'][name]={'exit':p.returncode,'log_sha256':hashlib.sha256(p.stdout+p.stderr).hexdigest()}
 print(name,p.returncode,flush=True)
 if p.returncode:print((p.stdout+p.stderr).decode(errors='replace')[-4000:]);raise SystemExit(p.returncode)
for mode,worker in [('normal','wizard-fixture'),('ubsan','wizard-fixture-ubsan')]:
 run('wizard-'+mode,[sys.executable,root/'tests/web/test_cf_wizard.py',binaries,worker])
 # Reuse the extensive existing worker fault suite, with only the explicit-target ABI adapted.
 code=(root/'tests/web/test_cf_bootstrap.py').read_text().replace('root=Path(__file__).resolve().parents[2]', 'root=Path('+repr(str(root))+')')
 code=code.replace("'confirmed-moxa1'","'confirmed-target'").replace("'00:90:e8:1f:4c:dd'","'01:90:e8:1f:4c:dd'")
 driver=out/('worker-'+mode+'.py');driver.write_text(code)
 run('worker-'+mode,[sys.executable,driver,binaries/worker,binaries/'4vrs-cf-rng-worker',out/('fixtures-'+mode),legacy/('codec-'+('host' if mode=='normal' else 'ubsan'))])
run('worker-dependencies',['ldd',binaries/'4vrs-cf-rng-worker'])
for p in [root/'tools/cf-wizard.py',root/'tools/cf-bootstrap.py',root/'src/host/cf_rng_writer.c']:
 evidence.setdefault('source_sha256',{})[str(p.relative_to(root))]=hashlib.sha256(p.read_bytes()).hexdigest()
evidence['worker_sha256']=hashlib.sha256((binaries/'4vrs-cf-rng-worker').read_bytes()).hexdigest()
(out/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
