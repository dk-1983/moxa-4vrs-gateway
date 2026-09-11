"""Local allowlisted full candidate and CF wizard. Never inspect device state."""
from pathlib import Path
import subprocess,json,hashlib,tarfile,zipfile,os,sys,tempfile
host=Path('/workspace/source-host');base=Path('/workspace/build/rng-slow-commit-v1-20260911');source=base/'source';out=base/'delivery';out.mkdir(exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
def run(name,args,env=None):
    log=base/(name+'.log')
    with log.open('wb') as stream:
        p=subprocess.run(list(map(str,args)),stdout=stream,stderr=subprocess.STDOUT,env=env,timeout=420)
    print(name,p.returncode,flush=True)
    if p.returncode:print(log.read_text(errors='replace')[-5000:]);sys.exit(p.returncode)
if sys.argv[1]=='installer-check':
    for mode in ['host','ubsan']:
        env=os.environ.copy()
        if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
        run('installer-'+mode,['sh',source/'tools/run-host-installer-tests.sh',source,base/('installer-'+mode)],env)
    sys.exit(0)
if sys.argv[1]=='qualify':
    run('build-installer',['python3',source/'tools/build-installer.py',source,base/'target/4vrs-install'])
    run('build-cf-wizard',['python3',host/'tools/build-cf-wizard.py',source,base/'dependency/mbedtls-3.6.7',base,base/'cf-wizard'])
    run('qualify-cf-wizard',['python3',host/'tools/qualify-cf-wizard.py',host,base/'cf-wizard','/workspace/build/cf-bootstrap',base/'cf-checks'])
    for mode in ['host','ubsan']:
        env=os.environ.copy()
        if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
        for suite in ['application','panel','installer']:
            run(suite+'-'+mode,['sh',source/('tools/run-host-'+suite+'-tests.sh'),source,base/(suite+'-'+mode)],env)
        run('full-web-integration-'+mode,['python3',source/'tests/web/test_web_integration.py',base/mode,source,source])
    sys.exit(0)

old=host/'build/release-review-20260911/delivery/4vrs-gateway-v2026.02.01-web-close-candidate.tar.gz'
assert sha(old.read_bytes())=='f519719cadca03b98ae00870581068961aee450ba79b60c57a5c893d2a471690'
previous=base/'previous';previous.mkdir(exist_ok=True)
with tarfile.open(old) as tar:
    for m in tar:
        assert m.isfile() and len(Path(m.name).parts)==2
        p=previous/Path(m.name).name;p.write_bytes(tar.extractfile(m).read());p.chmod(m.mode)
archive=out/'4vrs-gateway-v2026.02.01-rng-slow-commit-v1-candidate.tar.gz'
args=['python3',source/'tools/build-installer-package.py','--version','v2026.02.01']
for flag,name in [('installer','4vrs-install'),('gateway','4vrs-gateway'),('web','4vrs-web'),('rng','4vrs-rng'),('kdf','4vrs-kdf'),('init-script','4vrs-gateway.init'),('wrapper','4vrs-networking-wrapper'),('license-file','LICENSE.mbedtls')]:
    args+=['--'+flag,(base/'target'/name) if (base/'target'/name).exists() else previous/name]
with tempfile.TemporaryDirectory(prefix='package-repeat-',dir=base) as tmp:
    first=Path(tmp)/'first.tar.gz';second=Path(tmp)/'second.tar.gz'
    run('package',args+['--output',first])
    run('package-repeat',args+['--output',second])
    assert first.read_bytes()==second.read_bytes()
    if archive.exists():assert archive.read_bytes()==first.read_bytes(),'existing candidate differs; use a new delivery directory'
    else:archive.write_bytes(first.read_bytes())
inventory={};unpacked=base/'package';unpacked.mkdir(exist_ok=True)
with tarfile.open(archive) as t:
    for m in t:
        assert m.isfile() and len(Path(m.name).parts)==2
        data=t.extractfile(m).read();name=Path(m.name).name
        p=unpacked/name;p.write_bytes(data);p.chmod(m.mode)
        inventory[name]={'bytes':len(data),'sha256':sha(data),'mode':oct(m.mode)}
manifest=json.loads((unpacked/'manifest.json').read_text())
for f in manifest['files']:assert inventory[f['name']]['sha256']==f['sha256']
assert len(inventory)==11
run('package-abi',['python3',host/'tools/audit-web-abi.py',host/'build/clock-validation/target-libs',base/'package-abi.json',*[unpacked/n for n in ['4vrs-install','4vrs-gateway','4vrs-web','4vrs-rng','4vrs-kdf']]])
for mode in ['host','ubsan']:
    env=os.environ.copy()
    if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
    (base/('entry-'+mode)).mkdir(exist_ok=True)
    run('public-entry-'+mode,['python3',source/'tests/installer/test_entry.py',source,base/('entry-'+mode),unpacked],env)
    probe=base/('installer-'+mode)/'package-probe'
    assert probe.exists(),probe
    run('native-package-'+mode,[probe,unpacked])

def zipfiles(path,files):
    records={n:{'bytes':len(b),'sha256':sha(b)} for n,b in files.items()}
    files=dict(files);files['MANIFEST.json']=(json.dumps(records,indent=2)+'\n').encode()
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
        for n,b in sorted(files.items()):
            assert not any(x in Path(n).parts for x in ['.git','vendor','state','pending','attempt','start-permit','witness','witness-next','trial-policy','production-policy','__pycache__'])
            info=zipfile.ZipInfo(n,(2026,9,11,0,0,0));info.create_system=3;info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=(0o100755 if n=='4vrs-cf-rng-worker' else 0o100644)<<16;z.writestr(info,b)
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        for n,b in files.items():assert z.read(n)==b

cf={n:(host/'tools'/n).read_bytes() for n in ['cf-wizard.py','cf-bootstrap.py','cf-package.py']}
cf.update({'4vrs-cf-rng-worker':(base/'cf-wizard/4vrs-cf-rng-worker').read_bytes(),'gateway.tar.gz':archive.read_bytes(),'LICENSE':(host/'LICENSE').read_bytes(),'LICENSE.mbedtls':(previous/'LICENSE.mbedtls').read_bytes()})
for suffix in ['','.ru']:
    text=(host/('docs/cf-wizard'+suffix+'.md')).read_text(encoding='utf-8').replace('(cf-wizard.md)','(README.md)').replace('(cf-wizard.ru.md)','(README.ru.md)')
    cf['README'+suffix+'.md']=text.encode()
    cf['rng-autonomous-v1'+suffix+'.md']=(host/('docs/rng-autonomous-v1'+suffix+'.md')).read_bytes()
    cf['rng-autonomous-v1-hardware'+suffix+'.md']=(host/('docs/rng-autonomous-v1-hardware'+suffix+'.md')).read_bytes()
for p in (host/'docs').glob('rng-autonomous-v1*.md'):cf[p.name]=p.read_bytes()
cf['manifest.json']=(json.dumps({'format':1,'generic_target':True,'contains_seed':False,'contains_fixture_worker':False,'files':{n:{'sha256':sha(b),'bytes':len(b)} for n,b in cf.items()}},indent=2)+'\n').encode()
zipfiles(out/'4vrs-cf-wizard-v2026.02.01-rng-slow-commit-v1.zip',cf)

files={}
for name in ['src','tools','tests','deploy']:
    for p in (host/name).rglob('*'):
        if p.is_file() and '__pycache__' not in p.parts and (p.name=='.gitkeep' or p.suffix in ['.c','.h','.py','.sh','.js','.css','.html','.json','.md','.conf']):
            files[p.relative_to(host).as_posix()]=p.read_bytes()
for name in ['assets/icons','assets/help']:
    for p in (host/name).rglob('*'):
        if p.is_file():files[p.relative_to(host).as_posix()]=p.read_bytes()
for p in (host/'docs').glob('rng-autonomous-v1*.md'):files[p.relative_to(host).as_posix()]=p.read_bytes()
for p in (host/'docs/releases').glob('v2026.02.01-rng-slow-commit-v1*.md'):files[p.relative_to(host).as_posix()]=p.read_bytes()
for name in ['LICENSE','README.md','THIRD_PARTY.md']:files[name]=(host/name).read_bytes()
zipfiles(out/'4vrs-v2026.02.01-rng-slow-commit-v1-source-review.zip',files)
buildhash=json.loads((base/'source-manifest.json').read_text())
assert all(sha((host/n).read_bytes())==h for n,h in buildhash.items() if n.startswith('src/'))
report={'candidate':'v2026.02.01 rng-autonomous-v1','hardware':'not executed','published':False,'old_candidate_sha256':sha(old.read_bytes()),'inventory':inventory,'source_binary_match':'all compiled src hashes match workspace','policy':'autonomous consistent schema 3; retained transaction witness; eight diagnostic only','physical_NAND_write_limit':False}
(out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in sorted(out.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print('DELIVERY',str(out),flush=True)
