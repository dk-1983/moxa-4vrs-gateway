"""Allowlisted local diagnostic/review kit; no deployment or secret material."""
from pathlib import Path
import hashlib,json,sys,zipfile
root,build,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
abi=json.loads((build/'abi.json').read_text())
for b in abi['binaries']:
    assert not b['missing']
    assert sha((build/Path(b['path']).name).read_bytes())==b['sha256']
forbidden={'fork','kill','unlink','chmod','bind','listen','accept','write','execve','system'}
assert not forbidden & {n.split('@')[0] for n in abi['binaries'][0]['imports']}
items={'probe/4vrs-web-network-probe':(build/'4vrs-web-network-probe').read_bytes(),
       'review-only/4vrs-gateway-observer-fix':(build/'4vrs-gateway-observer-fix').read_bytes(),
       'README.ru.md':(root/'docs/forensics/web-lan-unavailable-desktop.md').read_bytes(),
       'abi.json':(build/'abi.json').read_bytes(),
       'source/web-network-readonly-probe.c':(root/'tools/web-network-readonly-probe.c').read_bytes(),
       'source/probe-query-only.c':(build/'source/probe-query-only.c').read_bytes(),
       'LICENSE':(root/'LICENSE').read_bytes(),
       'LICENSE.mbedtls':(root/'build/cf-bootstrap-native/MBEDTLS-LICENSE').read_bytes(),
       'NOTICE':b'Gateway includes unmodified Mbed TLS 3.6.7, Copyright The Mbed TLS Contributors, Apache-2.0 option. See LICENSE.mbedtls.\nProbe has no Mbed TLS or RNG dependency. Local review kit, not an installer.\n'}
assert b'PROBE_CASE' not in items['probe/4vrs-web-network-probe']
manifest={n:{'bytes':len(b),'sha256':sha(b)} for n,b in items.items()}
items['manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
items['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(items.items())).encode()
archive=out/'4vrs-web-network-diagnostic-moxa1-20260910.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for n,b in sorted(items.items()):
        info=zipfile.ZipInfo(n,(2026,9,10,0,0,0));info.create_system=3;info.external_attr=(0o100755 if n.startswith(('probe/','review-only/')) else 0o100644)<<16;info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,b)
with zipfile.ZipFile(archive) as z:assert z.testzip() is None and set(z.namelist())==set(items) and all(z.read(n)==b for n,b in items.items())
result=dict(archive=str(archive.resolve()),bytes=archive.stat().st_size,sha256=sha(archive.read_bytes()),files=manifest)
(out/'delivery.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
(out/'SHA256SUMS').write_text(result['sha256']+'  '+archive.name+'\n',encoding='ascii')
print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
