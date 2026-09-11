"""Offline package qualification against preserved 064851; no target execution."""
from pathlib import Path
import hashlib, json, shutil, subprocess, sys, tarfile
host, previous, fixed, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=False)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def run(name, args):
    p = subprocess.run(list(map(str, args)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out/(name+'.log')).write_bytes(p.stdout)
    print(name, p.returncode, flush=True)
    if p.returncode:
        print(p.stdout.decode(errors='replace')); raise SystemExit(p.returncode)
base = previous/'4vrs-gateway-v2026.02.01-console-candidate.tar.gz'
assert sha(base)=='921467d11e52712dd579b8b46c042fe15a18d1c5d3279cc1721732c630712963'
def unpack(archive, dest):
    dest.mkdir()
    records={}
    with tarfile.open(archive) as t:
        for m in t:
            p=Path(m.name)
            assert m.isfile() and len(p.parts)==2 and p.parts[0]=='4vrs-gateway-v2026.02.01'
            assert p.name not in records
            b=t.extractfile(m).read(); (dest/p.name).write_bytes(b); (dest/p.name).chmod(m.mode)
            records[p.name]={'size':len(b),'mode':oct(m.mode),'sha256':hashlib.sha256(b).hexdigest()}
    return records
old=out/'baseline'; oldrecords=unpack(base,old)
assert sha(fixed/'4vrs-gateway-observer-fix')=='68c8e994360a22cbb1cf8572ba6549238dc39b7a13983dbb99b6a7799a92e051'
# Ensure the qualified Gateway corresponds to current product source; ignore desktop/docs.
assert all(p.read_bytes()==(fixed/'source'/p.relative_to(host)).read_bytes() for p in (host/'src').rglob('*') if p.is_file())
original=previous.parent.parent/'source/src'
changes=[str(p.relative_to(original)) for p in original.rglob('*') if p.is_file() and p.read_bytes()!=(fixed/'source/src'/p.relative_to(original)).read_bytes()]
assert set(changes)=={'panel/gateway_panel.c','web/rng_client.c','web/web_gateway.c','web/web_gateway.h','network/gateway_network_runtime.h','network/gateway_network_runtime.c'}
equivalence=json.loads((previous/'formatting-equivalence.json').read_text())
assert equivalence['target_object_identical']
assert sha(original/'web/rng_client.c')==equivalence['old_source_sha256']
assert sha(fixed/'source/src/web/rng_client.c')==equivalence['new_source_sha256']
(out/'source-diff.json').write_text(json.dumps(changes,indent=2)+'\n')
archive=out/'4vrs-gateway-v2026.02.01-observer-candidate.tar.gz'
args=['python3',host/'tools/build-installer-package.py','--version','v2026.02.01']
for flag,name in [('installer','4vrs-install'),('web','4vrs-web'),('rng','4vrs-rng'),('kdf','4vrs-kdf'),('init-script','4vrs-gateway.init'),('wrapper','4vrs-networking-wrapper'),('license-file','LICENSE.mbedtls')]:
    args+=['--'+flag,old/name]
args+=['--gateway',fixed/'4vrs-gateway-observer-fix','--output',archive]
run('package',args);run('package-repeat',args[:-1]+[out/'repeat.tar.gz'])
assert archive.read_bytes()==(out/'repeat.tar.gz').read_bytes()
package=out/'candidate'; records=unpack(archive,package)
assert records.keys()==oldrecords.keys()
changed=[n for n in records if records[n]!=oldrecords[n]]
assert set(changed)=={'4vrs-gateway','manifest.json','SHA256SUMS'},changed
manifest=json.loads((package/'manifest.json').read_text())
assert manifest['format']==3 and manifest['version']=='v2026.02.01'
for f in manifest['files']:
    assert f['sha256']==records[f['name']]['sha256'] and f['size']==records[f['name']]['size']
    assert int(f['mode'],8)==int(records[f['name']]['mode'],8)
for line in (package/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ');assert records[name]['sha256']==digest
assert set(records)=={f['name'] for f in manifest['files']}|{'manifest.json','SHA256SUMS','NOTICE','LICENSE.mbedtls'}
run('abi',['python3',host/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',out/'abi.json',*[package/n for n in ['4vrs-install','4vrs-gateway','4vrs-web','4vrs-rng','4vrs-kdf']]])
run('native-package',[previous/'installer-host/package-probe',package])
damaged=out/'damaged';shutil.copytree(package,damaged)
data=bytearray((damaged/'4vrs-gateway').read_bytes());data[-1]^=1;(damaged/'4vrs-gateway').write_bytes(data)
p=subprocess.run([str(previous/'installer-host/package-probe'),str(damaged)],capture_output=True)
(out/'damaged-package.log').write_bytes(p.stdout+p.stderr)
assert p.returncode==1
run('public-entry',['python3',host/'tests/installer/test_entry.py',host,out,package])
# Reuse the existing manifest-verified Moxa1 archived filesystem loader, replacing
# its harness with the new baseline -> update -> rollback/no-op regression.
runner=(host/'tests/installer/run_release_archive.py').read_text()
runner=runner.replace("root/'tests/installer/managed_archive_probe.c'","root/'tests/installer/observer_upgrade_probe.c'")
runner=runner.replace('str(package),scenario','str(package.parent/"baseline"),str(package),scenario')
(out/'run-upgrade.py').write_text(runner)
for mode in ['host','ubsan']:
    import os
    env=os.environ.copy()
    if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
    command=['python3',str(out/'run-upgrade.py'),str(host),str(out/('upgrade-'+mode)),'/evidence/moxa1-v2026-01-01',str(package),'complete']
    p=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/('upgrade-'+mode+'.log')).write_bytes(p.stdout);print('upgrade-'+mode,p.returncode,flush=True)
    if p.returncode:print(p.stdout.decode(errors='replace'));raise SystemExit(p.returncode)
assert sha(base)=='921467d11e52712dd579b8b46c042fe15a18d1c5d3279cc1721732c630712963'
(out/'evidence.json').write_text(json.dumps({'baseline':{'path':str(base),'sha256':sha(base)},'candidate':{'name':archive.name,'size':archive.stat().st_size,'sha256':sha(archive)},'changed':changed,'files':records,'checks':'deterministic package, manifest/full inventory, five ELF ABI, native package, public entry, 064851 upgrade/rollback/retry/no-op host+UBSan'},indent=2)+'\n')
print('PASS',sha(archive),archive.stat().st_size,flush=True)
