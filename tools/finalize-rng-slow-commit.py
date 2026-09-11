"""Nonsecret evidence only; never export test state, credentials or runtime logs."""
from pathlib import Path
import json,hashlib,zipfile,tarfile
base=Path('/workspace/build/rng-slow-commit-v1-20260911');host=Path('/workspace/source-host');out=base/'delivery'
sha=lambda b:hashlib.sha256(b).hexdigest()
sourcezip=out/'4vrs-v2026.02.01-rng-slow-commit-v1-source-review.zip'
with zipfile.ZipFile(sourcezip) as z:
    files={n:z.read(n) for n in z.namelist() if n!='MANIFEST.json'}
files['tools/finalize-rng-slow-commit.py']=(host/'tools/finalize-rng-slow-commit.py').read_bytes()
for n in files:
    assert files[n]==(host/n).read_bytes(),n
files['MANIFEST.json']=(json.dumps({n:{'bytes':len(b),'sha256':sha(b)} for n,b in files.items()},indent=2)+'\n').encode()
with zipfile.ZipFile(sourcezip,'w',zipfile.ZIP_DEFLATED) as z:
    for n,b in sorted(files.items()):
        info=zipfile.ZipInfo(n,(2026,9,11,0,0,0));info.create_system=3;info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16;z.writestr(info,b)
checks={}
for mode in ['host','ubsan']:
    for name in ['test-web-core','test-rng-client','test_rng_product','test_rng_metadata','test_rng_autonomy','autonomy-gateway','application','panel','installer','full-web-integration','public-entry','native-package']:
        p=base/(name+'-'+mode+'.log');text=p.read_text()
        assert 'Traceback (most recent call last)' not in text and 'runtime error:' not in text and 'AssertionError' not in text,p
        checks[name+'-'+mode]={'result':'PASS','pass_lines':text.count('PASS')}
cf=json.loads((base/'cf-checks/evidence.json').read_text()) if (base/'cf-checks/evidence.json').exists() else None
if cf is None:
    candidates=list((base/'cf-checks').glob('*.json'))
    assert candidates,candidates
    cf=json.loads(candidates[0].read_text())
assert all(v['exit']==0 for v in cf['tests'].values())
checks['cf-wizard-normal-ubsan']={'result':'PASS','suites':list(cf['tests'])}
for name in ['4vrs-rng','4vrs-gateway','4vrs-web','4vrs-kdf','4vrs-install']:
    data=(base/'target'/name).read_bytes()
    assert data[:6]==b'\x7fELF\x01\x02'
    if name=='4vrs-rng':
        assert b'production-autonomous-v1' in data and b'service-arm' not in data
        assert b'NV_FAIL' not in data and b'NV_CRASH' not in data and b'NV_TEST_CLOCK' not in data and b'RNG_SLOW_' not in data
    assert data==(base/'package'/name).read_bytes()
checks['all-five-XScale-ABI']={'result':'PASS','evidence':'package-abi.json'}
for mode in ['host','ubsan']:
    for kind in ['baseline','fixed','rotation']:
        name=kind+'-slow-'+mode+'.json';record=json.loads((base/name).read_text())
        if kind=='baseline':assert len(record['cases'])==2 and all(c['result']=='SIGTERM-during-unhandled-commit-service-required' for c in record['cases'])
        elif kind=='fixed':assert len(record['cases'])==5 and all(c['gateway_continues'] for c in record['cases'])
        else:assert record['result']=='PASS' and not record['output_after_stop']
        (out/name).write_bytes((base/name).read_bytes())
        checks[kind+'-slow-'+mode]={'result':'PASS','evidence':name}
snapshot=json.loads((base/'source-manifest.json').read_text())
assert all(sha((host/n).read_bytes())==h for n,h in snapshot.items() if n.startswith('src/'))
for p in out.glob('*.zip'):
    with zipfile.ZipFile(p) as z:
        assert z.testzip() is None
        manifest=json.loads(z.read('MANIFEST.json'))
        for name,record in manifest.items():assert sha(z.read(name))==record['sha256']
        assert not any(any(part in Path(n).parts for part in ['state','pending','attempt','start-permit','witness','witness-next','owner.lock','trial-policy','production-policy','vendor','.git']) for n in z.namelist())
report={'checks':checks,'compiled_sources_match_workspace':True,'target_binaries_equal_package':True,
        'actual_realtime_substitution':'host LD_PRELOAD clock_gettime/time, epoch 0 / 2147483647 / 4294967295; generation never rewinds',
        'hardware_or_power_loss_tested':False,'real_UART_continuity_tested':False,
        'operating_policy':'autonomous consistent state; diagnostic threshold eight; no isolation; ambiguous writes fail closed',
        'production_runtime_secrets_exported':False,'no_device_access':True,'no_publication':True}
preserved=host/'build/rng-production-v1-review-20260911'
preserved_files={}
for line in (preserved/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ',1)
    assert sha((preserved/name).read_bytes())==digest,name
    preserved_files[name]=digest
report['previous_serviced_bundle_verified']=preserved_files
preserved=host/'build/rng-autonomous-v1-review-20260911';preserved_files={}
for line in (preserved/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ',1);assert sha((preserved/name).read_bytes())==digest,name
    preserved_files[name]=digest
report['previous_autonomous_bundle_verified']=preserved_files
report['slow_commit_reproduction']={
 'baseline_binary_sha256':{mode:{name:sha((Path('/workspace/build/rng-autonomous-v1-20260911')/mode/name).read_bytes()) for name in ['gateway-host','4vrs-rng']} for mode in ['host','ubsan']},
 'fixed_binary_sha256':{mode:{name:sha((base/mode/name).read_bytes()) for name in ['gateway-host','4vrs-rng']} for mode in ['host','ubsan']},
 'first_rng_response_ms':30000,'partial_frame_and_attach_ms':3000,'device_cause_confirmed':False,
 'installer_rng_schemas':[1,2,3],'device_markers_removed':False}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'package-abi.json').write_bytes((base/'package-abi.json').read_bytes())
(out/'compiled-source-sha256.json').write_text(json.dumps({n:h for n,h in snapshot.items() if n.startswith('src/')},indent=2)+'\n')
for suffix in ['.ru','']:
    ru=bool(suffix)
    for stem in ['rng-autonomous-v1','rng-autonomous-v1-hardware','rng-autonomous-v1-slow-commit','rng-autonomous-v1-moxa2-recovery']:
        (out/(stem+suffix+'.md')).write_bytes((host/'docs'/(stem+suffix+'.md')).read_bytes())
    text=('# v2026.02.01 — RNG autonomous-v1, комплект для рассмотрения\n\n' if ru else '# v2026.02.01 — RNG autonomous-v1 review bundle\n\n')
    text+=('Локальные проверки завершены: normal/UBSan, полный Web/Gateway, установщик, мастер CF и ABI пяти XScale ELF. Код и бинарники пакета сверены; тесты не обращались к устройствам.\n\n' if ru else 'Local checks completed: normal/UBSan, full Web/Gateway, installer, CF wizard and five XScale ELF ABIs. Compiled source and packaged binaries verified; no device access.\n\n')
    text+='- [Gateway installer](4vrs-gateway-v2026.02.01-rng-slow-commit-v1-candidate.tar.gz)\n- [CF wizard](4vrs-cf-wizard-v2026.02.01-rng-slow-commit-v1.zip)\n- [Source and policy](4vrs-v2026.02.01-rng-slow-commit-v1-source-review.zip)\n- [Validation](validation.json) · [Package inventory](review.json) · [SHA256SUMS](SHA256SUMS)\n\n'
    text+='[Policy](rng-autonomous-v1'+suffix+'.md) · [Hardware plan](rng-autonomous-v1-hardware'+suffix+'.md)\n\n'
    text+='[Slow commit: cause and fix](rng-autonomous-v1-slow-commit'+suffix+'.md) · [Moxa #2 recovery plan](rng-autonomous-v1-moxa2-recovery'+suffix+'.md)\n\n'
    text+=('Локально подтверждён SIGTERM от Gateway на 3-секундном тайм-ауте до установки обработчика broker. Исправление проверено с задержками 4,5 и 32 секунды: commit завершается перед выдачей или остановкой. Установщик поддерживает RNG schema 1/2/3 без изменения RNG-файлов.\n\n' if ru else 'Confirmed locally: Gateway SIGTERM at the three-second timeout before broker handler installation. Fixed behavior tested at 4.5 and 32 seconds: commit completes before output or stopping. Installer accepts RNG schemas 1/2/3 without changing RNG files.\n\n')
    text+=('**Модель:** согласованные state/witness запускаются автономно; восемь — диагностический порог, не предел ресурса CF. Неоднозначные записи требуют обслуживания без автоматического повтора. Мастер новой CF требует однократной явной активации.\n\n**Аппаратные препятствия:** питание/долговечность CF, реальные UART/Modbus при отказах, миграция и мастер на целевой CF, измерение обычных/аварийных записей. Политика и план RU/EN: docs/rng-autonomous-v1*.md в source-review.zip.\n\nПрежние пакеты сохранены. Устройства не изменены, релиз не опубликован.\n' if ru else '**Model:** consistent state/witness starts autonomously; eight is diagnostic, not CF endurance. Ambiguous writes require service without automatic retry. New CF requires one explicit activation.\n\n**Hardware gates:** CF power durability, actual UART/Modbus under faults, migration/wizard on real CF, measured normal/fault writes. RU/EN policy and plan: docs/rng-autonomous-v1*.md in source-review.zip.\n\nPrevious packages preserved. No device changes or publication.\n')
    (out/('README'+suffix+'.md')).write_text(text,encoding='utf-8')
(out/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in sorted(out.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print(json.dumps(report,indent=2))
