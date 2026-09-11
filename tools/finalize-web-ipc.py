"""Export only public summaries; validate source/package identity and prior bundles."""
from pathlib import Path
import json,hashlib,zipfile,tarfile
base=Path('/workspace/build/web-ipc-v1-20260911');host=Path('/workspace/source-host');out=base/'delivery'
sha=lambda b:hashlib.sha256(b).hexdigest()
checks={}
for mode in ['host','ubsan']:
    for name in ['test-web-core','test-rng-client','test_rng_product','test_rng_metadata','test_rng_autonomy','autonomy-gateway','application','panel','installer','full-web-integration','public-entry','native-package']:
        p=base/(name+'-'+mode+'.log');value=p.read_text()
        assert not any(s in value for s in ['Traceback (most recent call last)','runtime error:','AssertionError']),p
        checks[name+'-'+mode]={'result':'PASS','pass_lines':value.count('PASS')}
    name='ipc-gateway-'+mode+'.json';record=json.loads((base/name).read_text())
    assert record['result']=='PASS' and len(record['cycles'])==2
    assert record['ipc_denial']['gateway_continues'] and record['ipc_denial']['foreign_file_preserved'] and record['ipc_denial']['rng_ready']
    assert record['cycles'][1]['generation']==record['cycles'][0]['generation']+1
    (out/name).write_bytes((base/name).read_bytes())
    checks['ipc-gateway-'+mode]={'result':'PASS','evidence':name,'scope':'process crash and mock ports, not hardware'}
ipc=json.loads((base/'ipc-endpoint.json').read_text())
assert all(x['exit']==0 and '99' in x['result'] for x in ipc.values())
(out/'ipc-endpoint.json').write_bytes((base/'ipc-endpoint.json').read_bytes())
checks['ipc-endpoint-normal-ubsan']={'result':'PASS','assertions_per_mode':99,'evidence':'ipc-endpoint.json'}
order=json.loads((base/'rng-stop-order.json').read_text())
for mode in ['host','ubsan']:
    assert order['baseline-'+mode]['exit']==1 and order['fixed-'+mode]['exit']==0
(out/'rng-stop-order.json').write_bytes((base/'rng-stop-order.json').read_bytes())
checks['rng-stop-order-normal-ubsan']={'result':'PASS','evidence':'rng-stop-order.json','scope':'actual Gateway stop function; simulated immediate broker EOF scheduling'}
for mode in ['host','ubsan']:
    for kind in ['fixed','rotation']:
        name=kind+'-slow-'+mode+'.json';record=json.loads((base/name).read_text())
        if kind=='fixed':assert len(record['cases'])==5 and all(c['gateway_continues'] for c in record['cases'])
        else:assert record['result']=='PASS' and not record['output_after_stop']
        (out/name).write_bytes((base/name).read_bytes())
        checks[kind+'-slow-'+mode]={'result':'PASS','evidence':name}
cfpath=base/'cf-checks/evidence.json'
if not cfpath.exists():cfpath=next((base/'cf-checks').glob('*.json'))
cf=json.loads(cfpath.read_text());assert all(x['exit']==0 for x in cf['tests'].values())
checks['cf-wizard-normal-ubsan']={'result':'PASS','suites':list(cf['tests'])}
for name in ['4vrs-rng','4vrs-gateway','4vrs-web','4vrs-kdf','4vrs-install']:
    data=(base/'target'/name).read_bytes()
    assert data[:6]==b'\x7fELF\x01\x02' and data==(base/'package'/name).read_bytes()
    if name=='4vrs-rng':
        assert b'production-autonomous-v1' in data and b'service-arm' not in data
        assert all(flag not in data for flag in [b'NV_FAIL',b'NV_CRASH',b'NV_TEST_CLOCK',b'RNG_SLOW_'])
    if name=='4vrs-gateway':assert b'WEB_IPC_FAIL' not in data and b'WEB_IPC_TEST_ID' not in data
checks['all-five-XScale-ABI']={'result':'PASS','evidence':'package-abi.json'}
snapshot=json.loads((base/'source-manifest.json').read_text())
assert all(sha((host/n).read_bytes())==h for n,h in snapshot.items() if n.startswith('src/'))
for p in out.glob('*.zip'):
    with zipfile.ZipFile(p) as z:
        assert z.testzip() is None
        for name,record in json.loads(z.read('MANIFEST.json')).items():assert sha(z.read(name))==record['sha256']
        assert not any(any(part in Path(n).parts for part in ['state','pending','attempt','start-permit','witness','witness-next','owner.lock','trial-policy','production-policy','vendor','.git']) for n in z.namelist())
        if 'source-review' in p.name:
            for n in z.namelist():
                if n!='MANIFEST.json':assert z.read(n)==(host/n).read_bytes(),n
        else:
            assert z.read('gateway.tar.gz')==(out/'4vrs-gateway-v2026.02.01-web-ipc-v1-candidate.tar.gz').read_bytes()
            assert b'CF_FIXTURE' not in z.read('4vrs-cf-rng-worker')
report={'checks':checks,'compiled_sources_match_workspace':True,'target_binaries_equal_package':True,
        'hardware_or_power_loss_tested':False,'real_UART_continuity_tested':False,
        'operating_policy':'autonomous consistent state; diagnostic threshold eight; no isolation; ambiguous RNG writes fail closed',
        'production_runtime_secrets_exported':False,'no_device_access':True,'no_publication':True,
        'ipc':'protected persistent directory; fcntl owner lock; exact proc lookup; inode-checked cleanup',
        'hardware_series_required':'20 physical power cycles from zero',
        'previous_cycle_2':'FAIL autonomous startup; operator-reported HTTP 200 after manual socket rename',
        'hardware_evidence_files_independently_read':False}
for kind,directory in [('serviced','rng-production-v1'),('autonomous','rng-autonomous-v1'),('slow_commit','rng-slow-commit-v1')]:
    preserved=host/('build/'+directory+'-review-20260911');records={}
    for line in (preserved/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split('  ',1);assert sha((preserved/name).read_bytes())==digest,name;records[name]=digest
    report['previous_'+kind+'_bundle_verified']=records
prior=host/'build/rng-slow-commit-v1-review-20260911'
assert sha((prior/'4vrs-gateway-v2026.02.01-rng-slow-commit-v1-candidate.tar.gz').read_bytes())=='8e9fd95f2dd8392dea0adde301c69343a7a9443ccb2215714e7d0e5537a34978'
previous=json.loads((prior/'compiled-source-sha256.json').read_text())
rng={n:h for n,h in previous.items() if n.startswith('src/web/rng')}
assert rng and all(snapshot[n]==h for n,h in rng.items())
report['rng_sources_unchanged_from_slow_commit_v1']=rng
report['rng_markers_changed']=False
with tarfile.open(prior/'4vrs-gateway-v2026.02.01-rng-slow-commit-v1-candidate.tar.gz') as t:
    old={Path(m.name).name:t.extractfile(m).read() for m in t if m.isfile()}
unchanged={}
for name in ['4vrs-rng','4vrs-web','4vrs-kdf','4vrs-install']:
    assert old[name]==(base/'target'/name).read_bytes()
    unchanged[name]=sha(old[name])
report['target_binaries_identical_to_slow_commit_v1']=unchanged
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'package-abi.json').write_bytes((base/'package-abi.json').read_bytes())
(out/'compiled-source-sha256.json').write_text(json.dumps({n:h for n,h in snapshot.items() if n.startswith('src/')},indent=2)+'\n')
for suffix in ['.ru','']:
    for pattern in ['rng-autonomous-v1*'+suffix+'.md','web-ipc-*'+suffix+'.md']:
        for p in (host/'docs').glob(pattern):(out/p.name).write_bytes(p.read_bytes())
    notes=(host/'docs/releases'/('v2026.02.01-web-ipc-v1'+suffix+'.md')).read_text(encoding='utf-8')
    notes=notes.replace('(../web-ipc-', '(web-ipc-').replace('(v2026.02.01-web-ipc-v1', '(release-notes')
    (out/('release-notes'+suffix+'.md')).write_text(notes,encoding='utf-8')
    ru=bool(suffix)
    title='# v2026.02.01-web-ipc-v1 — '+('комплект для рассмотрения' if ru else 'review bundle')+'\n\n'
    text=('Исправлено восстановление Web IPC после аварийного завершения на постоянной /tmp. Локальные normal/UBSan, полный Gateway/Web, установщик, мастер CF и XScale ABI проверены. RNG-код совпадает с прежним slow-commit-v1.\n\n' if ru else 'Fixed Web IPC recovery after abnormal termination on persistent /tmp. Local normal/UBSan, full Gateway/Web, installer, CF wizard and XScale ABI checked. RNG sources match previous slow-commit-v1.\n\n')
    text+='- [Gateway installer](4vrs-gateway-v2026.02.01-web-ipc-v1-candidate.tar.gz)\n- [CF wizard](4vrs-cf-wizard-v2026.02.01-web-ipc-v1.zip)\n- [Source review](4vrs-v2026.02.01-web-ipc-v1-source-review.zip)\n- [Validation](validation.json) · [Inventory](review.json) · [SHA256SUMS](SHA256SUMS)\n\n'
    text+='[IPC](web-ipc-lifecycle'+suffix+'.md) · [Hardware plan](web-ipc-power-cycle-plan'+suffix+'.md) · [RNG policy](rng-autonomous-v1'+suffix+'.md)\n\n'
    text+=('Аппаратный цикл 2 прежнего кандидата остаётся FAIL; HTTP 200 после ручного переименования не меняет оценку. Нужна новая серия 20 физических power-cycle и отдельная квалификация питания/CF/сети. Аппаратные сведения получены от оператора, указанные desktop-файлы независимо не прочитаны.\n\nПрежние пакеты сохранены. Устройства и RNG-маркеры не изменялись; релиз не опубликован.\n' if ru else 'Previous candidate hardware cycle 2 remains FAIL; HTTP 200 after manual renaming does not change the result. Twenty new physical power cycles and separate power/CF/network qualification are required. Hardware facts are operator-reported; the referenced desktop files were not independently read.\n\nPrevious packages preserved. No device or RNG marker changes; no publication.\n')
    (out/('README'+suffix+'.md')).write_text(title+text,encoding='utf-8')
(out/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in sorted(out.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print(json.dumps({'checks':checks,'rng_sources_unchanged':len(rng),'delivery':str(out)},indent=2))
