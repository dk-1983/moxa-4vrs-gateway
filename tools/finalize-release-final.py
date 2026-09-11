"""Validate the exact review bundle; preserve scoped hardware acceptance and historical limits."""
from pathlib import Path
import json,hashlib,tarfile,zipfile,shutil
root=Path('/workspace/source-host');base=Path('/workspace/build/release-final-20260911');out=base/'delivery-qualified'
sha=lambda b:hashlib.sha256(b).hexdigest()
prior=root/'build/web-ipc-v1-review-20260911'
preserved={}
for line in (prior/'SHA256SUMS').read_text().splitlines():
    h,n=line.split('  ',1);assert sha((prior/n).read_bytes())==h;preserved[n]=h
assert preserved['4vrs-cf-wizard-v2026.02.01-web-ipc-v1.zip']=='da91f6aa2460c57146098b9e946da27d4c5ab5472924817273423b33c99f12f1'
assert preserved['4vrs-gateway-v2026.02.01-web-ipc-v1-candidate.tar.gz']=='92e35241d9ee8d3fa8c9420803b55f0c793a8c1755ecf9f5e5133eb9babd0100'
snapshot=json.loads((base/'source-manifest.json').read_text());previous=json.loads((prior/'compiled-source-sha256.json').read_text())
assert all(sha((root/n).read_bytes())==h for n,h in snapshot.items())
assert all(snapshot[n]==h for n,h in previous.items() if not n.startswith('src/installer/'))
def members(path):
    with tarfile.open(path) as t:return {Path(m.name).name:t.extractfile(m).read() for m in t if m.isfile()}
archive=out/'4vrs-gateway-v2026.02.01-final-review-candidate.tar.gz';current=members(archive)
old=members(prior/'4vrs-gateway-v2026.02.01-web-ipc-v1-candidate.tar.gz')
unchanged={}
for name in ['4vrs-gateway','4vrs-web','4vrs-rng','4vrs-kdf']:
    assert current[name]==old[name];unchanged[name]=sha(current[name])
for name in [*unchanged,'4vrs-install']:
    assert current[name]==(base/'target'/name).read_bytes() and current[name][:6]==b'\x7fELF\x01\x02'
assert current['4vrs-install']!=old['4vrs-install']
checks={}
for mode in ['host','ubsan']:
    for name in ['installer','public-entry','native-package']:
        text=(base/(name+'-'+mode+'.log')).read_text()
        assert not any(s in text for s in ['Traceback (most recent call last)','runtime error:','AssertionError'])
        if name=='public-entry':assert 'journal metadata without contents' in text
        checks[name+'-'+mode]='PASS'
kit=json.loads((out/'delivered-kit.json').read_text());assert kit['result']=='PASS' and kit['pin']==sha(archive.read_bytes())
kitpath=out/'4vrs-cf-wizard-v2026.02.01-final-review.zip';assert kit['zip_sha256']==sha(kitpath.read_bytes())
for p in out.glob('*.zip'):
    with zipfile.ZipFile(p) as z:
        assert z.testzip() is None
        for n,record in json.loads(z.read('MANIFEST.json')).items():assert sha(z.read(n))==record['sha256']
        assert not any(any(part in Path(n).parts for part in ['state','witness','pending','attempt','production-policy','trial-policy','fresh-rng-input','vendor','.git']) for n in z.namelist())
        if 'source-review' in p.name:
            for n in z.namelist():
                if n!='MANIFEST.json':assert z.read(n)==(root/n).read_bytes(),n
        else:
            assert z.read('gateway.tar.gz')==archive.read_bytes()
            with zipfile.ZipFile(prior/'4vrs-cf-wizard-v2026.02.01-web-ipc-v1.zip') as oldzip:assert z.read('4vrs-cf-rng-worker')==oldzip.read('4vrs-cf-rng-worker')
checks['delivered-zip-unzip-manifests-package-load-worker-corruption']='PASS'
checks['target-ABI-and-nested-binary-identity']='PASS'
inventory=json.loads((out/'review.json').read_text())
inventory['base_assets_archive_sha256']=inventory.pop('old_candidate_sha256')
inventory['hardware']={'runtime':'reuse unchanged web-ipc-v1 evidence: agreed 10/10 completed-commit power cycles and one LAN recovery','new_installer_diagnostics':'operator-confirmed platform verify/network-service-health repair followed by healthy no-op'}
(out/'review.json').write_text(json.dumps(inventory,indent=2)+'\n')
report={'checks':checks,'release_ready':True,'remaining_blocker':None,'release_scope':'healthy consistent CF, completed-commit power recovery, stated P1/P2 TCP and LAN coverage; explicit first activation',
        'runtime_unchanged_from_web_ipc_v1':unchanged,'cf_worker_unchanged':True,'compiled_sources_match_workspace':True,
        'hardware_power_cycles':{'completed':10,'required':10,'scope':'completed NV commit, healthy consistent CF','repeat_required':False},
        'spare_card_results':'operator-confirmed first activation/install and three restarts; desktop files unavailable locally',
        'no_device_access_this_task':True,'publication':False,'secret_material_included':False,'previous_bundle_verified':preserved,
        'cf_helper_generation':'package-release-final.py stamps exact gateway archive SHA256 into copied cf-package.py before both manifests; source template retains historical fixture pin'}
report['hardware_noop']={'source':'operator-supplied hardware account 2026-09-11; raw desktop snapshots not independently read','historical_active_slot':0,'historical_result_utc':1789141385,'historical_plan_differences':0,'historical_branch':'platform-verify-failed','diagnostic_repair_health':'network-service-health','narrow_network_signal':'not established','healthy_repeat_result':3,'gateway_rng_pids_and_generation_preserved':True,'web_pid_preservation_proven':False,'gate':'closed'}
prior_review=root/'build/release-final-review-20260911'
for line in (prior_review/'SHA256SUMS').read_text().splitlines():
    h,n=line.split('  ',1);assert sha((prior_review/n).read_bytes())==h
assert archive.read_bytes()==(prior_review/archive.name).read_bytes()
report['previous_final_review_preserved']=True
report['installer_archive_unchanged_from_hardware_test']=True
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'compiled-source-sha256.json').write_text(json.dumps(snapshot,indent=2)+'\n')
shutil.copyfile(base/'package-abi.json',out/'package-abi.json')
for p in (root/'docs').glob('release-final*.md'):shutil.copyfile(p,out/p.name)
(out/'forensics').mkdir(exist_ok=True)
for name in ['moxa2-web-ipc-power-cycles-20260911.ru.md','moxa2-web-ipc-lan-return-20260911.ru.md']:shutil.copyfile(root/'docs/forensics'/name,out/'forensics'/name)
for suffix in ['.ru','']:
    notes=(root/'docs/releases'/('v2026.02.01-final-review'+suffix+'.md')).read_text(encoding='utf-8').replace('(../release-final','(release-final')
    (out/('release-notes'+suffix+'.md')).write_text(notes,encoding='utf-8')
    title='# v2026.02.01 — '+('комплект для рассмотрения' if suffix else 'review bundle')+'\n\n'
    text=('Мастер исправлен и проверен из выдаваемого ZIP. Runtime сохранён; установщик дополнен диагностикой. Критерий no-op закрыт аппаратно; готово к выпуску в области матрицы. Историческая сетевая первопричина не установлена. Повтор завершённых 10/10 power-cycle не требуется.\n\n' if suffix else 'Wizard fixed and checked from the distributed ZIP. Runtime preserved; installer adds diagnostics. Hardware no-op acceptance is closed; ready for release within the matrix scope. Historical network root cause remains unproven. Completed 10/10 power cycles do not need repeating.\n\n')
    text+='[Matrix](release-final'+suffix+'.md) · [Commissioning](release-final-commissioning'+suffix+'.md) · [No-op diagnosis](release-final-noop'+suffix+'.md)\n\n'
    text+='- [Installer](4vrs-gateway-v2026.02.01-final-review-candidate.tar.gz)\n- [CF wizard](4vrs-cf-wizard-v2026.02.01-final-review.zip)\n- [Sources](4vrs-v2026.02.01-final-review-source-review.zip)\n- [Validation](validation.json) · [ZIP check](delivered-kit.json) · [SHA256SUMS](SHA256SUMS)\n\n'
    text+=('Прежние пакеты сохранены. Устройства не изменялись, релиз не опубликован.\n' if suffix else 'Previous packages preserved. No device changes or publication.\n')
    (out/('README'+suffix+'.md')).write_text(title+text,encoding='utf-8')
(out/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.relative_to(out).as_posix()+'\n' for p in sorted(out.rglob('*')) if p.is_file() and p!=out/'SHA256SUMS'))
print(json.dumps({'checks':checks,'remaining_blocker':report['remaining_blocker'],'installer_sha256':sha(archive.read_bytes()),'cf_zip_sha256':sha(kitpath.read_bytes())},indent=2))
