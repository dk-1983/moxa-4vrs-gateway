"""Deterministic allowlisted offline Ubuntu kit; never reads card contents or generates seed."""
from pathlib import Path
import hashlib,json,struct,sys,zipfile
root,binaries,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
baseline=root/'build/cf-bootstrap-moxa1-20260910/4vrs-cf-bootstrap-moxa1-20260910.zip'
assert sha(baseline.read_bytes())=='1fd33434f3a39135b0a38154c0f78f498a7c2b52a3edc79d67c5115485ee4f83'
worker=(binaries/'4vrs-cf-rng-worker').read_bytes()
assert worker[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',worker,18)[0]==62
assert b'confirmed-target' in worker and b'getrandom' in worker
for marker in [b'CF_RANDOM',b'CF_FAIL',b'CF_CRASH',b'__wrap_getrandom',b'CF_IO']:
 assert marker not in worker
with zipfile.ZipFile(baseline) as z:license=z.read('LICENSE.mbedtls')
files={'4vrs-cf-rng-worker':worker,'cf-wizard.py':(root/'tools/cf-wizard.py').read_bytes(),
       'cf-bootstrap.py':(root/'tools/cf-bootstrap.py').read_bytes(),
       'cf-package.py':(root/'tools/cf-package.py').read_bytes(),
       'gateway.tar.gz':(root/'build/web-close-candidate-20260910/4vrs-gateway-v2026.02.01-web-close-candidate.tar.gz').read_bytes(),
       'LICENSE':(root/'LICENSE').read_bytes(),'LICENSE.mbedtls':license,
       'NOTICE':b'4VRS CompactFlash wizard, local service candidate 2026-09-10.\nIncludes unmodified Mbed TLS 3.6.7 SHA256/platform code, Copyright The Mbed TLS Contributors.\nApache-2.0 option selected; see LICENSE.mbedtls. No vendor SDK/toolchain or seed included.\n',
       'source/cf_rng_writer.c':(root/'src/host/cf_rng_writer.c').read_bytes()}
for suffix in ['', '.ru']:
 text=(root/('docs/cf-wizard'+suffix+'.md')).read_text(encoding='utf-8')
 text=text.replace('(cf-wizard.md)','(README.md)').replace('(cf-wizard.ru.md)','(README.ru.md)')
 files['README'+suffix+'.md']=text.encode()
files['manifest.json']=(json.dumps({'format':1,'platform':'Ubuntu 24.04 x86-64','generic_target':True,
 'contains_seed':False,'contains_fixture_worker':False,'baseline_sha256':sha(baseline.read_bytes()),
 'files':{n:{'bytes':len(b),'sha256':sha(b),'mode':'0755' if n=='4vrs-cf-rng-worker' else '0644'} for n,b in sorted(files.items())}},indent=2)+'\n').encode()
files['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(files.items())).encode()
public=bytes(255 if i==5 else i for i in range(32))
assert all(public not in b for b in files.values())
assert not any(Path(n).name in ('state','pending','seed','trial-policy','owner.lock') for n in files)
archive=out/'4vrs-cf-wizard-install-update-ubuntu24-20260910.zip'
with zipfile.ZipFile(archive,'x') as z:
 for name,data in sorted(files.items()):
  info=zipfile.ZipInfo(name,(2026,9,10,0,0,0));info.create_system=3;info.compress_type=zipfile.ZIP_DEFLATED
  info.external_attr=(0o100755 if name=='4vrs-cf-rng-worker' else 0o100644)<<16;z.writestr(info,data)
with zipfile.ZipFile(archive) as z:assert z.testzip() is None and all(z.read(n)==b for n,b in files.items())
e={'archive':archive.name,'bytes':archive.stat().st_size,'sha256':sha(archive.read_bytes()),'entries':len(files),
   'worker_sha256':sha(worker),'production_seed_generated':False,'fixture_worker_included':False,
   'baseline_preserved':sha(baseline.read_bytes())}
(out/'delivery.json').write_text(json.dumps(e,indent=2)+'\n')
(out/'SHA256SUMS').write_text(e['sha256']+'  '+archive.name+'\n')
print(json.dumps(e,indent=2))
