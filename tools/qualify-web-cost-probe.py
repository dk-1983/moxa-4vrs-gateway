"""Fresh standalone probe builds; does not rebuild or execute the product/target."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path('/workspace/source');host=Path('/workspace/source-host');out=Path(sys.argv[1]);out.mkdir(exist_ok=False)
records=[]
def run(name,args):
 print('START',name,flush=True)
 with (out/(name+'.log')).open('w') as log:r=subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT)
 records.append(dict(name=name,command=list(map(str,args)),exit=r.returncode));(out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
 if r.returncode:print((out/(name+'.log')).read_text()[-4000:]);raise SystemExit(r.returncode)
 print('PASS',name,flush=True)
sources={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for d in ['src/web','tests/web'] for p in sorted((root/d).rglob('*')) if p.is_file()}
for n in ['tools/build-web-tls.sh','tools/prepare-web-tls.py','tools/audit-web-abi.py','tools/qualify-web-cost-probe.py']:sources[n]=hashlib.sha256((root/n).read_bytes()).hexdigest()
(out/'source-manifest.json').write_text(json.dumps(sources,indent=2)+'\n')
run('dependency',['python3',root/'tools/prepare-web-tls.py',host/'vendor/web-foundation/mbedtls-3.6.7.tar.bz2',out/'dependency'])
tls=out/'dependency/mbedtls-3.6.7'
for mode in ['host','ubsan','target']:
 dest=out/mode
 run('tls-'+mode,['sh',root/'tools/build-web-tls.sh',root,tls,dest/'tls',mode])
 cc='/usr/local/xscale_be/bin/xscale_be-gcc' if mode=='target' else 'cc'
 flags=['-std=c99','-Os','-Wall','-Wextra','-Werror','-DMBEDTLS_CONFIG_FILE="web/mbedtls_config.h"','-I'+str(root/'src'),'-I'+str(tls/'include')]
 if mode=='target':flags+=['-mcpu=xscale','-mbig-endian','-msoft-float']
 else:flags+=['-DWEB_HOST_TEST','-Wno-misleading-indentation']
 if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
 run('cost-'+mode,[cc,*flags,root/'src/web/web_cost_probe.c',dest/'tls/libtls.a','-lrt','-o',dest/'4vrs-web-cost-probe'])
 if mode!='target':
  run('fault-build-'+mode,[cc,*flags,root/'src/web/web_cost_probe.c',root/'tests/web/probe_faults.c','-Wl,--wrap=open,--wrap=read,--wrap=close,--wrap=setpriority',dest/'tls/libtls.a','-lrt','-o',dest/'fault-probe'])
  run('fault-tests-'+mode,['python3',root/'tests/web/test_cost_probe.py',dest])
  run('core-build-'+mode,[cc,*flags,'-Wl,--wrap=web_random,--wrap=time,--wrap=fsync',*[root/('src/web/'+n+'.c') for n in ['web_protocol','web_security','web_http','web_certificate']],root/'tests/web/test_web_core.c',dest/'tls/libtls.a','-lrt','-o',dest/'test-web-core'])
  run('core-'+mode,[dest/'test-web-core'])
run('abi',['python3',root/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'abi.json',out/'target/4vrs-web-cost-probe'])
artifacts={str(p.relative_to(out)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'md5':hashlib.md5(p.read_bytes()).hexdigest()} for p in [out/m/'4vrs-web-cost-probe' for m in ['host','ubsan','target']]}
(out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
for name,digest in sources.items():assert hashlib.sha256((host/name).read_bytes()).hexdigest()==digest,name
print('COMPLETE: standalone probe only; no device access.',flush=True)
