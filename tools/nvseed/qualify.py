"""Offline isolated NV prototype qualification; never deploy or export seeds."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root=Path('/workspace/source'); host=Path('/workspace/source-host'); out=Path(sys.argv[1]); out.mkdir(exist_ok=False)
records=[]
def run(name, args):
    print('START',name,flush=True)
    with (out/(name+'.log')).open('w') as log:
        p=subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,timeout=180)
    records.append({'name':name,'command':list(map(str,args)),'exit':p.returncode})
    (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
    if p.returncode:
        print((out/(name+'.log')).read_text()[-5000:]); raise SystemExit(p.returncode)
    print('PASS',name,flush=True)
names=['tools/nvseed/config.h','tools/nvseed/probe.c','tools/nvseed/test.py','tools/nvseed/qualify.py','tools/prepare-web-tls.py','tools/audit-web-abi.py']
sources={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in names}
(out/'source-manifest.json').write_text(json.dumps(sources,indent=2)+'\n')
run('dependency',['python3',root/'tools/prepare-web-tls.py',host/'vendor/web-foundation/mbedtls-3.6.7.tar.bz2',out/'dependency'])
tls=out/'dependency/mbedtls-3.6.7'; manifest={}
for mode in ['host','ubsan','target']:
    dest=out/mode; dest.mkdir(); objects=[]
    cc='/usr/local/xscale_be/bin/xscale_be-gcc' if mode=='target' else 'cc'
    ar='/usr/local/xscale_be/bin/xscale_be-ar' if mode=='target' else 'ar'
    flags=['-std=c99','-Os','-Wall','-Wextra','-DMBEDTLS_CONFIG_FILE="nvseed/config.h"','-I'+str(root/'tools'),'-I'+str(tls/'include'),'-I'+str(tls/'library')]
    if mode=='target': flags+=['-mcpu=xscale','-mbig-endian','-msoft-float']
    if mode=='ubsan': flags+=['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
    # Entire pinned library sees the isolated config, no edits to vendor sources.
    script=dest/'build.sh'
    import shlex
    lines=['set -eu']
    for c in sorted((tls/'library').glob('*.c')):
        obj=dest/(c.stem+'.o'); objects.append(obj)
        lines.append(shlex.join([cc,*flags,'-c',str(c),'-o',str(obj)]))
    lines.append(shlex.join([ar,'rcs',str(dest/'libnv.a'),*map(str,objects)]))
    binary=dest/'4vrs-nvseed-probe'
    lines.append(shlex.join([cc,*flags,'-Werror',*(['-DNV_FIXTURE'] if mode!='target' else []),str(root/'tools/nvseed/probe.c'),str(dest/'libnv.a'),'-Wl,--wrap=fork','-o',str(binary)]))
    script.write_text('\n'.join(lines)+'\n'); run('build-'+mode,['sh',script])
    if mode!='target':run('test-'+mode,['python3',root/'tools/nvseed/test.py',binary])
    b=binary.read_bytes();manifest[str(binary.relative_to(out))]={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'md5':hashlib.md5(b).hexdigest()}
run('abi',['python3',root/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'abi.json',out/'target/4vrs-nvseed-probe'])
(out/'artifact-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for n,h in sources.items(): assert hashlib.sha256((host/n).read_bytes()).hexdigest()==h,n
print('COMPLETE: isolated NV prototype only; no target access.',flush=True)
