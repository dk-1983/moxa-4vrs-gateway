"""Accompaniment kit: preserve baseline ZIP and worker, update inspection flow."""
from pathlib import Path
import hashlib,json,re,sys,zipfile
root,baseline,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
baseline_sha='1fd33434f3a39135b0a38154c0f78f498a7c2b52a3edc79d67c5115485ee4f83'
worker_sha='2a87bf2354215e727e9e0c936818a333d5ce4feddce0201219eaa6c28212b69d'
old=baseline.read_bytes();assert sha(old)==baseline_sha
with zipfile.ZipFile(baseline) as z:
    kit={name:z.read(name) for name in ['4vrs-cf-rng-worker','LICENSE','LICENSE.mbedtls','NOTICE','source/cf_rng_writer.c']}
assert sha(kit['4vrs-cf-rng-worker'])==worker_sha
assert kit['source/cf_rng_writer.c']==(root/'src/host/cf_rng_writer.c').read_bytes()
kit['cf-bootstrap.py']=(root/'tools/cf-bootstrap.py').read_bytes()
kit['target-confirmation.json']=(root/'deploy/cf-bootstrap/moxa1-target-confirmation.json').read_bytes()
target=json.loads(kit['target-confirmation.json']);assert target['confirmed'] is False
assert target['cf_uuid']=='2ad4f17a-0eb1-bc47-8c4e-b23d70f39b52'
for suffix in ['', '.ru']:
    text=(root/('docs/cf-bootstrap'+suffix+'.md')).read_text(encoding='utf-8')
    def link(m):
        label,path=m.groups()
        if path in ['cf-bootstrap.md','cf-bootstrap.ru.md']:return '['+label+']('+path.replace('cf-bootstrap','README')+')'
        return label+' (`docs/'+path+'` in repository)'
    kit['README'+suffix+'.md']=re.sub(r'\[([^\]]+)\]\(([^)]+)\)',link,text).encode()
manifest={'format':1,'baseline_sha256':baseline_sha,'worker_unchanged':True,'contains_seed':False,
          'files':{n:{'bytes':len(b),'sha256':sha(b)} for n,b in sorted(kit.items())}}
kit['manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
kit['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(kit.items())).encode()
items={'kit/'+n:b for n,b in kit.items()}
items['baseline/'+baseline.name]=old
items['START-HERE.ru.md']='''# Комплект сопровождения CF Moxa #1

Ubuntu 24.04 x86-64. Архив baseline сохранён для прослеживаемости; работать
из каталога **kit**, где лежат прежний проверенный worker и обновлённый frontend.
UUID: 2ad4f17a-0eb1-bc47-8c4e-b23d70f39b52; MAC: 00:90:E8:1F:4C:F1.
Физическая запись ещё не квалифицирована. Seed/state и квитанции в комплекте нет.

1. Распаковать на PC вне CF, в доверенный каталог root. Из корня комплекта:

```sh
sha256sum -c SHA256SUMS
cd kit
sha256sum -c SHA256SUMS
sudo chmod 0755 ./4vrs-cf-rng-worker
python3 cf-bootstrap.py --help
```

2. По README.ru.md определить актуальные CF_DISK, CF_PART, CF_MOUNT; проверить
   RO=1 для диска и раздела и единственный ext3 ro,noload,nodev,nosuid,noexec mount.
   Не подставлять исторические /dev/sde или номера дисков. Ожидаемые размеры:
   4009549824 / 4008501248 байт, offset 1048576, reader 05e3:0743.
3. Оставить `target-confirmation.json` с **confirmed:false**. Сначала:

```sh
sudo python3 cf-bootstrap.py inspect \\
  --disk "$CF_DISK" --partition "$CF_PART" --mount "$CF_MOUNT" \\
  --target-confirmation target-confirmation.json --receipt inspection.json
```

4. Сверить свежую квитанцию с физической картой: UUID, геометрию, пути, RO,
   `clean_readonly_inspection:true`, для первой подготовки `rng:absent`.
   Только после этого изменить confirmed на true. Контрольная сумма этого
   JSON после осознанного изменения уже отличается от поставочной — это ожидаемо;
   не изменять контрольные суммы executable/frontend.
5. Запись требует отдельного разрешения. Порядок rw/prepare/RO verify и
   безопасного извлечения — в README.ru.md. Инструмент не монтирует, не снимает
   RO и не разрешает запись по одному подтверждённому UUID. При любой ошибке
   остановиться; не создавать фиктивную receipt, не удалять state/pending,
   не повторять prepare поверх существующего каталога.

Shutdown/перенос карты/RO inspect и разрешение записи — отдельные этапы
обслуживания. Trial-policy не меняется. Комплект не публикует релиз.
'''.encode('utf-8')
items['START-HERE.md']='''# Moxa #1 CF accompaniment kit

Ubuntu 24.04 x86-64. Use **kit/**; baseline/ contains the unchanged earlier ZIP
for provenance. The worker is unchanged; the updated frontend permits initial
read-only inspection with confirmed:false. No seed, state or receipt is supplied.

1. Unpack on the trusted PC outside CF. Run `sha256sum -c SHA256SUMS`, then
   `cd kit`, `sha256sum -c SHA256SUMS`, `sudo chmod 0755 ./4vrs-cf-rng-worker`.
2. Follow kit/README.md to identify actual device paths and establish the sole
   ext3 read-only/no-replay mount, disk and partition RO=1.
3. Keep target-confirmation.json **confirmed:false**. Run inspect as documented.
4. Compare the fresh receipt with the physical card, including UUID, sizes, RO,
   clean status and absence of RNG. Only then change confirmed to true. That
   deliberate JSON edit changes its delivery checksum; keep executable checksums.
5. Obtain separate authorization for the exact write window; follow README.md
   for preparation, RO verification and safe unmount. Do not manufacture a receipt
   or retry over an existing RNG directory. Physical writes remain unqualified.

Expected UUID 2ad4f17a-0eb1-bc47-8c4e-b23d70f39b52; MAC 00:90:E8:1F:4C:F1.
Device shutdown/card transfer, read-only inspection and write authorization
are separate maintenance stages. This kit does not enable trial-policy.
'''.encode()
public=bytes(255 if i==5 else i for i in range(32))
assert all(public not in b for b in kit.values())
assert not any(Path(n).name in ['state','pending','inspection.json','seed','trial-policy'] for n in items)
items['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(items.items())).encode()
archive=out/'4vrs-cf-bootstrap-moxa1-confirmed-uuid-20260910.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for n,b in sorted(items.items()):
        info=zipfile.ZipInfo(n,(2026,9,10,0,0,0));info.create_system=3
        info.external_attr=(0o100755 if n=='kit/4vrs-cf-rng-worker' else 0o100644)<<16
        info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,b)
with zipfile.ZipFile(archive) as z:assert z.testzip() is None and all(z.read(n)==b for n,b in items.items())
result=dict(archive=str(archive.resolve()),bytes=archive.stat().st_size,sha256=sha(archive.read_bytes()),
            baseline_sha256=baseline_sha,worker_sha256=worker_sha,confirmed=False,inspection_receipt_included=False,
            production_seed_generated=False,entries=len(items))
(out/'delivery.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
(out/'SHA256SUMS').write_text(result['sha256']+'  '+archive.name+'\n',encoding='ascii')
print(json.dumps(result,indent=2))
