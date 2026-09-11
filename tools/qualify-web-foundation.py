"""Local-only qualification in the prepared Linux SDK container."""
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
root=Path('/workspace/source');host=Path('/workspace/source-host');out=Path(sys.argv[1] if len(sys.argv)>1 else '/workspace/build/web-qualification')
out.mkdir(exist_ok=False)
records=[]
def run(name,args,env=None):
    result=subprocess.run(list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
    (out/(name+'.log')).write_text(result.stdout)
    records.append({'name':name,'command':list(map(str,args)),'exit':result.returncode})
    (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
    print(name, 'PASS' if result.returncode==0 else 'FAIL', flush=True)
    if result.returncode:
        print(result.stdout,flush=True)
        raise SystemExit(result.returncode)
manifest={}
for folder in ('src','tools','tests','deploy'):
    for p in (root/folder).rglob('*'):
        if not p.is_file() or '__pycache__' in p.parts:continue
        relative=p.relative_to(root)
        digest=hashlib.sha256(p.read_bytes()).hexdigest()
        assert digest==hashlib.sha256((host/relative).read_bytes()).hexdigest(),str(relative)
        manifest[str(relative)]=digest
(out/'source-manifest.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
run('dependency',['python3',root/'tools/prepare-web-tls.py',host/'vendor/web-foundation/mbedtls-3.6.7.tar.bz2',out/'dependency'])
for mode in ('host','ubsan','target'):
    run('foundation-'+mode,['python3',root/'tools/run-web-foundation.py',root,out/mode,mode])
    run('tls-build-'+mode,['sh',root/'tools/build-web-tls.sh',root,out/'dependency/mbedtls-3.6.7',out/('tls-'+mode),mode])
    if mode!='target':
        run('tls-integration-'+mode,['python3',root/'tests/web/test_tls_integration.py',out/('tls-'+mode)/'tls-probe',out/('integration-'+mode)])
for mode in ('host','ubsan'):
    env=os.environ.copy()
    if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -g'
    for suite in ('persistence','panel','network-application'):
        run(suite+'-'+mode,['sh',root/('tools/run-host-'+suite+'-tests.sh'),root,out/(suite+'-'+mode)],env)
run('package-v1',['python3',root/'tests/installer/test_package.py'])
run('package-v2-contract',['python3',root/'tests/installer/test_web_package_contract.py'])
run('abi',['python3',root/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'abi.json',
           out/'tls-target/tls-probe',out/'target/web-control',out/'target/key-repeat',out/'target/keypad-event-probe'])
print('All local qualification checks passed; no ARM execution.',flush=True)
