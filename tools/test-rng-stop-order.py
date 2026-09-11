from pathlib import Path
import subprocess,json,zipfile
root=Path('/workspace/source-host');base=Path('/workspace/build/web-ipc-v1-20260911')
old=root/'build/rng-slow-commit-v1-review-20260911/4vrs-v2026.02.01-rng-slow-commit-v1-source-review.zip'
baseline=base/'baseline-stop-web-gateway.c'
with zipfile.ZipFile(old) as z:baseline.write_bytes(z.read('src/web/web_gateway.c'))
records={}
for mode in ['host','ubsan']:
    for kind,path in [('baseline',baseline),('fixed',root/'src/web/web_gateway.c')]:
        binary=base/(kind+'-stop-order-'+mode)
        flags=['-std=c99','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-format-truncation','-ffunction-sections','-fdata-sections','-I'+str(root/'src'),'-DGATEWAY_SOURCE="'+str(path)+'"']
        if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
        subprocess.run(['cc',*flags,root/'tests/web/test_rng_stop_order.c','-Wl,--gc-sections','-o',binary],check=True)
        p=subprocess.run([binary],capture_output=True);result=(p.stdout+p.stderr).decode()
        assert p.returncode==(1 if kind=='baseline' else 0) and 'runtime error:' not in result
        records[kind+'-'+mode]={'exit':p.returncode,'result':result.strip()}
(base/'rng-stop-order.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records,indent=2))
