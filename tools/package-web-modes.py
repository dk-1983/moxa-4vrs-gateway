"""Full format-3 candidate, strict diff to optimized, preserved observer/064851, offline tests."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tarfile
host,build,previous,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=False)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(name,args,env=None):
 p=subprocess.run(list(map(str,args)),env=env,capture_output=True);(out/(name+'.log')).write_bytes(p.stdout+p.stderr);print(name,p.returncode,flush=True)
 if p.returncode:print((p.stdout+p.stderr).decode(errors='replace')[-4000:]);raise SystemExit(p.returncode)
def unpack(path,dest):
 dest.mkdir();records={}
 with tarfile.open(path) as t:
  for m in t:
   p=Path(m.name);assert m.isfile() and len(p.parts)==2 and p.parts[0]=='4vrs-gateway-v2026.02.01' and p.name not in records
   data=t.extractfile(m).read();f=dest/p.name;f.write_bytes(data);f.chmod(m.mode)
   records[p.name]={'size':len(data),'mode':m.mode,'sha256':sha(f),'md5':hashlib.md5(data).hexdigest()}
 return records
base_paths={
 'optimized':host/'build/web-optimized-candidate-20260910/4vrs-gateway-v2026.02.01-web-optimized-candidate.tar.gz',
 '064851':host/'build/rng-product-candidate-20260909-064851/4vrs-gateway-v2026.02.01-console-candidate.tar.gz',
 'observer':host/'build/observer-installer-candidate-20260910/4vrs-gateway-v2026.02.01-observer-candidate.tar.gz'}
base_hashes={'optimized':'663c615ff5f0fa70afc25352a5e1c00c0f6358ed6fe268c938e38b1b27ba652a','064851':'921467d11e52712dd579b8b46c042fe15a18d1c5d3279cc1721732c630712963','observer':'03528b149b6b30fb10ce198d84c0433d570497211aea1bd0a83ef478b18689ae'}
baselines={}
for name,p in base_paths.items():assert sha(p)==base_hashes[name];baselines[name]=unpack(p,out/name)
base=out/'optimized';archive=out/'4vrs-gateway-v2026.02.01-http-mode-candidate.tar.gz'
for n in ['4vrs-rng','4vrs-kdf']:assert sha(build/'target'/n)==baselines['observer'][n]['sha256']
assets=json.loads((build/'target/web-assets.json').read_text());oldassets={a['route']:a for a in json.loads((previous/'web/target/web-assets.json').read_text())}
for a in assets:
 if a['route'] not in ['/','/app.js','/extended.js','/style.css','/help/en.html','/help/ru.html']:assert a['sha256']==oldassets[a['route']]['sha256']
args=['python3',host/'tools/build-installer-package.py','--version','v2026.02.01']
for flag,name in [('installer','4vrs-install'),('gateway','4vrs-gateway'),('rng','4vrs-rng'),('kdf','4vrs-kdf'),('init-script','4vrs-gateway.init'),('wrapper','4vrs-networking-wrapper'),('license-file','LICENSE.mbedtls')]:args+=['--'+flag,(build/'target'/name) if name in ['4vrs-install','4vrs-gateway'] else base/name]
args+=['--web',build/'target/4vrs-web','--output',archive]
run('package',args);run('repeat',args[:-1]+[out/'repeat.tar.gz']);assert archive.read_bytes()==(out/'repeat.tar.gz').read_bytes()
package=out/'candidate';records=unpack(archive,package)
assert records.keys()==baselines['observer'].keys()
assert {n for n in records if records[n]!=baselines['observer'][n]}=={'4vrs-install','4vrs-gateway','4vrs-web','manifest.json','SHA256SUMS'}
manifest=json.loads((package/'manifest.json').read_text());assert manifest['format']==3
for f in manifest['files']:assert f['sha256']==records[f['name']]['sha256'] and f['size']==records[f['name']]['size'] and int(f['mode'],8)==records[f['name']]['mode']
for line in (package/'SHA256SUMS').read_text().splitlines():digest,name=line.split('  ');assert digest==records[name]['sha256']
assert set(records)=={f['name'] for f in manifest['files']}|{'manifest.json','SHA256SUMS','LICENSE.mbedtls','NOTICE'}
run('package-abi',['python3',host/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'abi.json',*[package/n for n in ['4vrs-install','4vrs-gateway','4vrs-web','4vrs-rng','4vrs-kdf']]])
run('native-package',[previous/'installer-host/package-probe',package])
run('public-entry',['python3',host/'tests/installer/test_entry.py',host,out,package])
for baseline in base_paths:
 runner=(host/'tests/installer/run_release_archive.py').read_text().replace("root/'tests/installer/managed_archive_probe.c'","root/'tests/installer/web_modes_upgrade_probe.c'")
 runner=runner.replace('str(package),scenario',repr(str(out/baseline))+',str(package),scenario')
 path=out/('upgrade-'+baseline+'.py');path.write_text(runner)
 for mode in ['host','ubsan']:
  env=os.environ.copy()
  if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
  run('upgrade-'+baseline+'-'+mode,['python3',path,host,out/('fixture-'+baseline+'-'+mode),'/evidence/moxa1-v2026-01-01',package,'complete'],env)
for name,p in base_paths.items():assert sha(p)==base_hashes[name]
(out/'evidence.json').write_text(json.dumps({'archive':{'name':archive.name,'size':archive.stat().st_size,'sha256':sha(archive)},'files':records,'preserved_baselines':base_hashes,'changed_vs_optimized':['4vrs-install','4vrs-gateway','4vrs-web','manifest.json','SHA256SUMS'],'assets':assets,'checks':'format3/full inventory/ABI/deterministic/native/public entry/optimized+064851+observer upgrade automatic rollback retry no-op return in host+UBSan'},indent=2)+'\n')
print('PACKAGE PASS',archive.stat().st_size,sha(archive),flush=True)
