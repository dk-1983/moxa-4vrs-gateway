from pathlib import Path
import subprocess,json
root=Path('/workspace/source-host');out=Path('/workspace/build/web-ipc-v1-20260911');out.mkdir(exist_ok=True)
results={}
for mode in ['host','ubsan']:
    binary=out/('ipc-endpoint-'+mode)
    flags=['-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-format-truncation','-DWEB_HOST_TEST','-I'+str(root/'src')]
    if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
    subprocess.run(['cc',*flags,root/'src/web/web_ipc_endpoint.c',root/'tests/web/test_web_ipc_endpoint.c','-o',binary],check=True)
    p=subprocess.run([binary],capture_output=True);(out/('ipc-endpoint-'+mode+'.log')).write_bytes(p.stdout+p.stderr)
    print(p.stdout.decode()+p.stderr.decode(),flush=True);assert p.returncode==0
    results[mode]={'exit':p.returncode,'result':p.stdout.decode().strip()}
(out/'ipc-endpoint.json').write_text(json.dumps(results,indent=2)+'\n')
