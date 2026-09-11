"""Build comparison-only artifacts. No product modification or target execution."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path('/workspace/source');host=Path('/workspace/source-host');out=Path(sys.argv[1]);out.mkdir(exist_ok=False);records=[]
def run(name,args):
 print('START',name,flush=True)
 with (out/(name+'.log')).open('w') as log:r=subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,timeout=180)
 records.append(dict(name=name,command=list(map(str,args)),exit=r.returncode));(out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
 if r.returncode:print((out/(name+'.log')).read_text()[-4000:]);raise SystemExit(r.returncode)
 print('PASS',name,flush=True)
names=['tools/kdf-cached.h','tools/kdf-compare.c','tools/qualify-kdf-compare.py','tools/build-web-tls.sh','tools/prepare-web-tls.py','tools/audit-web-abi.py','src/web/mbedtls_config.h','src/web/web_security.h','src/web/web_protocol.h','src/web/tls_probe.c']
sources={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in names}
(out/'source-manifest.json').write_text(json.dumps(sources,indent=2)+'\n')
run('dependency',['python3',root/'tools/prepare-web-tls.py',host/'vendor/web-foundation/mbedtls-3.6.7.tar.bz2',out/'dependency'])
tls=out/'dependency/mbedtls-3.6.7'
def arr(b):return '{'+','.join(str(x) for x in b)+'}'
cases=[]
for length in [12,24,63,64,65,128]:
 for rounds in [1,2,17,1000]:
  p=bytes((i*37+length)%256 for i in range(length));s=bytes(range(16));cases.append((p,s,rounds))
cases.append(('Пароль-проверка'.encode(),bytes([255]*16),17))
header='struct vector {unsigned char password[128]; unsigned int plen; unsigned char salt[16]; unsigned int rounds; unsigned char expected[32];};\nstatic const struct vector vectors[]={\n'
for p,s,n in cases:header+='{'+arr(p)+','+str(len(p))+','+arr(s)+','+str(n)+','+arr(hashlib.pbkdf2_hmac('sha256',p,s,n))+'},\n'
header+='};\n'
p=b'disposable-probe-password';s=b'probe-salt-16byte'[:16]
for name,b in [('measure_password',p),('measure_salt',s),('measure_expected',hashlib.pbkdf2_hmac('sha256',p,s,600000))]:header+='static const unsigned char '+name+'[]='+arr(b)+';\n'
(out/'kdf-vectors.h').write_text(header)
manifest={}
for mode in ['host','ubsan','target']:
 for opt in ['Os','O2']:
  dest=out/(mode+'-'+opt);dest.mkdir()
  script=(root/'tools/build-web-tls.sh').read_text().replace('-Os','-'+opt);build=dest/'build-tls.sh';build.write_text(script)
  run('tls-'+mode+'-'+opt,['sh',build,root,tls,dest/'tls',mode])
  cc='/usr/local/xscale_be/bin/xscale_be-gcc' if mode=='target' else 'cc'
  flags=['-std=c99','-'+opt,'-Wall','-Wextra','-Werror','-DMBEDTLS_CONFIG_FILE="web/mbedtls_config.h"','-I'+str(root/'src'),'-I'+str(root/'tools'),'-I'+str(out),'-I'+str(tls/'include')]
  if mode=='target':flags+=['-mcpu=xscale','-mbig-endian','-msoft-float']
  else:flags+=['-Wno-misleading-indentation']
  if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
  for cached in ([False] if opt=='Os' else [False,True]):
   variant=opt+('-cached' if cached else '-upstream');binary=dest/('4vrs-kdf-'+variant);extra=['-DKDF_CACHED'] if cached else []
   run('build-'+mode+'-'+variant,[cc,*flags,*extra,'-DKDF_VARIANT="'+variant+'"',root/'tools/kdf-compare.c',dest/'tls/libtls.a','-lrt','-o',binary])
   if mode!='target':
    for action in ['selftest','measure']:run(mode+'-'+variant+'-'+action,[binary,'--'+action])
   b=binary.read_bytes();manifest[str(binary.relative_to(out))]={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'md5':hashlib.md5(b).hexdigest()}
run('abi',['python3',root/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'abi.json',*[out/n for n in manifest if n.startswith('target-')]])
(out/'artifact-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for n,h in sources.items():assert hashlib.sha256((host/n).read_bytes()).hexdigest()==h,n
print('COMPLETE: KDF comparison only; 600000 rounds unchanged.',flush=True)
