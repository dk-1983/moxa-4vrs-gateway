"""Nonsecret evidence only; never export test state, credentials or runtime logs."""
from pathlib import Path
import json,hashlib,zipfile,tarfile
base=Path('/workspace/build/rng-production-v1-20260911');host=Path('/workspace/source-host');out=base/'delivery'
sha=lambda b:hashlib.sha256(b).hexdigest()
sourcezip=out/'4vrs-v2026.02.01-rng-production-v1-source-review.zip'
with zipfile.ZipFile(sourcezip) as z:
    files={n:z.read(n) for n in z.namelist() if n!='MANIFEST.json'}
files['tools/finalize-rng-production.py']=(host/'tools/finalize-rng-production.py').read_bytes()
for n in files:
    assert files[n]==(host/n).read_bytes(),n
files['MANIFEST.json']=(json.dumps({n:{'bytes':len(b),'sha256':sha(b)} for n,b in files.items()},indent=2)+'\n').encode()
with zipfile.ZipFile(sourcezip,'w',zipfile.ZIP_DEFLATED) as z:
    for n,b in sorted(files.items()):
        info=zipfile.ZipInfo(n,(2026,9,11,0,0,0));info.create_system=3;info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16;z.writestr(info,b)
checks={}
for mode in ['host','ubsan']:
    for name in ['test-web-core','test-rng-client','test_rng_product','test_rng_metadata','test_rng_production','production-gateway','application','panel','installer','full-web-integration','public-entry','native-package']:
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
        assert b'production-v1' in data and b'service-arm' in data
        assert b'NV_FAIL' not in data and b'NV_CRASH' not in data and b'NV_TEST_CLOCK' not in data
    assert data==(base/'package'/name).read_bytes()
checks['all-five-XScale-ABI']={'result':'PASS','evidence':'package-abi.json'}
snapshot=json.loads((base/'source-manifest.json').read_text())
assert all(sha((host/n).read_bytes())==h for n,h in snapshot.items() if n.startswith('src/'))
for p in out.glob('*.zip'):
    with zipfile.ZipFile(p) as z:
        assert z.testzip() is None
        manifest=json.loads(z.read('MANIFEST.json'))
        for name,record in manifest.items():assert sha(z.read(name))==record['sha256']
        assert not any(any(part in Path(n).parts for part in ['state','pending','attempt','start-permit','owner.lock','trial-policy','production-policy','vendor','.git']) for n in z.namelist())
report={'checks':checks,'compiled_sources_match_workspace':True,'target_binaries_equal_package':True,
        'actual_realtime_substitution':'host LD_PRELOAD clock_gettime/time, epoch 0 / 2147483647 / 4294967295; quota unchanged',
        'hardware_or_power_loss_tested':False,'real_UART_continuity_tested':False,
        'service_time_attestation':'administrative external 24h isolation; no automatic replenishment',
        'production_runtime_secrets_exported':False,'no_device_access':True,'no_publication':True}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'package-abi.json').write_bytes((base/'package-abi.json').read_bytes())
(out/'compiled-source-sha256.json').write_text(json.dumps({n:h for n,h in snapshot.items() if n.startswith('src/')},indent=2)+'\n')
for suffix in ['.ru','']:
    ru=bool(suffix)
    text=('# v2026.02.01 — RNG production-v1, комплект для рассмотрения\n\n' if ru else '# v2026.02.01 — RNG production-v1 review bundle\n\n')
    text+=('Локальные проверки завершены: normal/UBSan, полный Web/Gateway, установщик, мастер CF и ABI пяти XScale ELF. Код и бинарники пакета сверены; тесты не обращались к устройствам.\n\n' if ru else 'Local checks completed: normal/UBSan, full Web/Gateway, installer, CF wizard and five XScale ELF ABIs. Compiled source and packaged binaries verified; no device access.\n\n')
    text+='- [Gateway installer](4vrs-gateway-v2026.02.01-rng-production-v1-candidate.tar.gz)\n- [CF wizard](4vrs-cf-wizard-v2026.02.01-rng-production-v1.zip)\n- [Source and policy](4vrs-v2026.02.01-rng-production-v1-source-review.zip)\n- [Validation](validation.json) · [Package inventory](review.json) · [SHA256SUMS](SHA256SUMS)\n\n'
    text+=('**Ограничение:** восьмипоколенная сохраняемая квота без автоматического пополнения; возобновление только после 24 часов внешне измеренной изоляции и новой случайности. Каждый запуск требует одноразового сервисного разрешения; service-arm не сбрасывает квоту. Не unattended restart и не гарантия ресурса CF/NAND.\n\n**Аппаратные препятствия:** долговечность CF при физическом питании/ошибках; доверенный конфиденциальный сервисный канал и внешний учёт времени; реальная сохранность UART/Modbus при отказах; установка/откат и мастер на целевом устройстве. План и политика RU/EN находятся в source-review.zip, docs/rng-production-v1*.md.\n\nПрежний кандидат сохранён с SHA256 f519719cadca03b98ae00870581068961aee450ba79b60c57a5c893d2a471690. Устройства не изменены, релиз не опубликован.\n' if ru else '**Restriction:** eight persistent generation slots, no automatic replenishment; renewal only after 24 externally measured isolation hours and fresh entropy. Every startup needs one-use service permission; service-arm preserves quota. No unattended restart or CF/NAND endurance guarantee.\n\n**Hardware gates:** CF durability under physical power/I/O faults; confidential trusted service channel and external time; real UART/Modbus continuity under failure; target installation/rollback and CF wizard. RU/EN policy and plan are in source-review.zip, docs/rng-production-v1*.md.\n\nPrevious candidate preserved: SHA256 f519719cadca03b98ae00870581068961aee450ba79b60c57a5c893d2a471690. No device changes or publication.\n')
    (out/('README'+suffix+'.md')).write_text(text,encoding='utf-8')
(out/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in sorted(out.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print(json.dumps(report,indent=2))
