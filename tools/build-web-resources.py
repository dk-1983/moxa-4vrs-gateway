"""Build an isolated Web candidate, reusing verified unchanged TLS/RNG outputs.
Usage: root previous-web-build output mode [web-only]. Never executes target ELF.
"""
from pathlib import Path
import hashlib,json,re,shutil,subprocess,sys
host,base,out=map(Path,sys.argv[1:4]);mode=sys.argv[4]
assert mode in ('host','ubsan','target')
source=out/'source'
for directory in ('src','tools','tests','deploy'):shutil.copytree(host/directory,source/directory,dirs_exist_ok=True)
dest=out/mode;dest.mkdir(parents=True,exist_ok=True)
def run(args):
 result=subprocess.run(list(map(str,args)),capture_output=True)
 with (dest/'build.log').open('ab') as log:log.write(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace')[-4000:])
code=(source/'tools/build-web-integration.py').read_text()
tls_call="run(['sh',root/'tools/build-web-tls.sh',root,tls,out/'tls',mode])"
rng_call="run(['python3',root/'tools/build-rng.py',root,tls,out,mode])"
assert tls_call in code and rng_call in code
code=code.replace(tls_call,"shutil.copytree(base/mode/'tls',out/'tls',dirs_exist_ok=True)")
code=code.replace(rng_call,"shutil.copy2(base/mode/'4vrs-rng',out/'4vrs-rng')")
code=code[code.index("run(['python3',root/'tools/embed-web-assets.py'"):]
if len(sys.argv)>5 and sys.argv[5]=='web-only':
 code=code[:code.index('\ngateway=re.findall')]
exec(compile(code,'build-web-integration.py','exec'),{'run':run,'root':source,'tls':base/'dependency/mbedtls-3.6.7','out':dest,'mode':mode,'assets':host/'assets/icons','shutil':shutil,'base':base,'re':re})
records={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source/'src').rglob('*') if p.is_file()}
(dest/'source-manifest.json').write_text(json.dumps(records,indent=2)+'\n')
print('Built',mode,'without changing preserved baseline')
