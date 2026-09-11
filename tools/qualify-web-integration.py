"""Local qualification of the real integrated components, with no ARM execution."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
root=Path('/workspace/source');host=Path('/workspace/source-host');out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False);records=[]
def run(name,args,env=None):
 result=subprocess.run(list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
 (out/(name+'.log')).write_text(result.stdout);records.append({'name':name,'command':list(map(str,args)),'exit':result.returncode});(out/'commands.json').write_text(json.dumps(records,indent=2)+'\n');print(name,'PASS' if not result.returncode else 'FAIL',flush=True)
 if result.returncode:print(result.stdout,flush=True);raise SystemExit(result.returncode)
manifest={}
for folder in ('src','tools','tests','deploy'):
 for p in (root/folder).rglob('*'):
  if not p.is_file() or '__pycache__' in p.parts:continue
  rel=p.relative_to(root);digest=hashlib.sha256(p.read_bytes()).hexdigest();assert digest==hashlib.sha256((host/rel).read_bytes()).hexdigest(),str(rel);manifest[str(rel)]=digest
(out/'source-manifest.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
run('dependency',['python3',root/'tools/prepare-web-tls.py',host/'vendor/web-foundation/mbedtls-3.6.7.tar.bz2',out/'dependency'])
for mode in ('host','ubsan','target'):
 run('build-'+mode,['python3',root/'tools/build-web-integration.py',root,out/'dependency/mbedtls-3.6.7',out/mode,mode,host/'assets/icons'])
 if mode!='target':
  run('core-'+mode,[out/mode/'test-web-core'])
  run('rng-client-'+mode,[out/mode/'test-rng-client'])
  run('rng-product-'+mode,['python3',root/'tests/web/test_rng_product.py',out/mode/'4vrs-rng'])
  run('rng-metadata-'+mode,['python3',root/'tests/web/test_rng_metadata.py',out/mode/'4vrs-rng'])
  run('kdf-exec-'+mode,['python3',root/'tests/web/test_kdf_exec.py',out/mode/'4vrs-kdf'])
  run('integration-'+mode,['python3',root/'tests/web/test_web_integration.py',out/mode,root,host])
for mode in (() if len(sys.argv)>2 and sys.argv[2]=='components' else ('host','ubsan')):
 env=os.environ.copy()
 if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -g'
 for suite in ('application','persistence','panel','network-application','diagnostics','clock-provider'):
  run(suite+'-'+mode,['sh',root/('tools/run-host-'+suite+'-tests.sh'),root,out/(suite+'-'+mode)],env)
run('abi',['python3',root/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'abi.json',out/'target/4vrs-gateway',out/'target/4vrs-web',out/'target/4vrs-rng',out/'target/4vrs-kdf'])
print('Local integration qualification complete; no hardware/ARM execution.',flush=True)
