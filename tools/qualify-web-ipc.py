"""Local-only production-v1 build; never accesses devices or state on real CF."""
from pathlib import Path
import subprocess, sys, json, os, shutil, hashlib
root=Path('/workspace/source-host')
out=Path('/workspace/build/web-ipc-v1-20260911')
out.mkdir(exist_ok=True)
def run(name,args):
    result=subprocess.run(list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/(name+'.log')).write_bytes(result.stdout)
    print(name,result.returncode,flush=True)
    if result.returncode:
        print(result.stdout.decode(errors='replace')[-5000:]);sys.exit(result.returncode)
if sys.argv[1]=='build':
    host=root;root=out/'source';root.mkdir(exist_ok=True)
    for name in ['src','tools','tests','deploy','assets/icons','assets/help']:
        shutil.copytree(host/name,root/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
    hashes={}
    for name in ['src','tools','tests','deploy','assets/icons','assets/help']:
        for p in (host/name).rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts:
                rel=p.relative_to(host);h=hashlib.sha256(p.read_bytes()).hexdigest()
                assert hashlib.sha256((root/rel).read_bytes()).hexdigest()==h
                hashes[str(rel)]=h
    (out/'source-manifest.json').write_text(json.dumps(hashes,indent=2)+'\n')
    if not (out/'dependency/mbedtls-3.6.7').exists():
        run('dependency',['python3',root/'tools/prepare-web-tls.py',host/'vendor/web-foundation/mbedtls-3.6.7.tar.bz2',out/'dependency'])
    for mode in sys.argv[2:] or ['host','ubsan','target']:
        run('build-'+mode,['python3',root/'tools/build-web-integration.py',root,out/'dependency/mbedtls-3.6.7',out/mode,mode,root/'assets/icons'])
elif sys.argv[1]=='tests':
    for mode in ['host','ubsan']:
        for name in ['test-web-core','test-rng-client']:
            run(name+'-'+mode,[out/mode/name])
        for name in ['test_rng_product','test_rng_metadata','test_rng_autonomy']:
            run(name+'-'+mode,['python3',root/('tests/web/'+name+'.py'),out/mode/'4vrs-rng'])
        run('autonomy-gateway-'+mode,['python3',root/'tests/web/test_rng_autonomy_gateway.py',out/mode])
elif sys.argv[1]=='abi':
    run('abi',['python3',root/'tools/audit-web-abi.py',root/'build/clock-validation/target-libs',out/'abi.json',*[out/'target'/n for n in ['4vrs-gateway','4vrs-web','4vrs-rng','4vrs-kdf']]])
elif sys.argv[1]=='audit':
    records=json.loads((out/'source-manifest.json').read_text())
    changed=[n for n,h in records.items() if hashlib.sha256((root/n).read_bytes()).hexdigest()!=h]
    print('Changed since build:',changed)
elif sys.argv[1]=='tests-clock':
    for mode in ['host','ubsan']:
        run('test_rng_autonomy-'+mode,['python3',root/'tests/web/test_rng_autonomy.py',out/mode/'4vrs-rng'])
elif sys.argv[1]=='tests-gateway':
    for mode in ['host','ubsan']:
        run('autonomy-gateway-'+mode,['python3',root/'tests/web/test_rng_autonomy_gateway.py',out/mode])
elif sys.argv[1]=='rng-relink':
    snapshot=out/'source';records=json.loads((out/'source-manifest.json').read_text())
    for n in ['src/web/rng_main.c','src/web/rng_nv.c','src/web/rng_nv.h']:
        shutil.copyfile(root/n,snapshot/n);records[n]=hashlib.sha256((root/n).read_bytes()).hexdigest()
    (out/'source-manifest.json').write_text(json.dumps(records,indent=2)+'\n')
    for mode in ['host','ubsan','target']:
        cc='/usr/local/xscale_be/bin/xscale_be-gcc' if mode=='target' else 'cc'
        flags=['-std=c99','-Os','-Wall','-Wextra','-DMBEDTLS_CONFIG_FILE="web/rng_config.h"','-I'+str(snapshot/'src'),'-I'+str(out/'dependency/mbedtls-3.6.7/include')]
        flags+=['-mcpu=xscale','-mbig-endian','-msoft-float'] if mode=='target' else ['-DWEB_HOST_TEST','-Wno-misleading-indentation']
        if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
        run('rng-relink-'+mode,[cc,*flags,snapshot/'src/web/rng_main.c',snapshot/'src/web/rng_nv.c',out/mode/'rng-objects/librng.a','-lrt','-o',out/mode/'4vrs-rng'])
