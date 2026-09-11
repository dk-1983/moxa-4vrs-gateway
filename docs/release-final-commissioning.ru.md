# Первичный ввод: CF → установка → явная активация → Web

[English](release-final-commissioning.md) · [Матрица](release-final.ru.md)

Выполняйте ввод в эксплуатацию в согласованное окно обслуживания. До записи проверьте модель, MAC выбранного прибора и UUID его карты. IP и UUID на иллюстрациях — примеры, не целевые значения. Мастер подготавливает CF; установка и первичная активация на приборе выполняются отдельно.

## Что подготовить

Нужны Moxa UC-7420-LX Plus с заводской прошивкой 1.6, CF и USB-картридер, Ubuntu 24.04 x86-64 с доступом root/sudo и консольный доступ к Moxa. В Windows можно использовать Ubuntu 24.04 в WSL2, но сначала USB-картридер должен быть передан в WSL и виден в `lsblk`; буква диска Windows не является устройством Linux. Мастер откажет, если не может безопасно определить системный диск или целевую карту. В таком случае используйте обычную Ubuntu 24.04. Не используйте CF с единственной копией нужных данных для полной очистки.

Выполняйте команды по одной; при ошибке остановитесь. Каталоги должны быть новыми. Скачанный мастер уже содержит полный `gateway.tar.gz`: отдельная загрузка установщика для этой ветки не требуется.

```sh
sudo apt-get update
sudo apt-get install python3 util-linux fdisk e2fsprogs udev unzip curl
mkdir 4vrs-v2026.02.01-download
cd 4vrs-v2026.02.01-download
curl -fLO https://github.com/dk-1983/moxa-4vrs-gateway/releases/download/v2026.02.01/4vrs-cf-wizard-v2026.02.01-ubuntu24-docs-r2.zip
curl -fLO https://github.com/dk-1983/moxa-4vrs-gateway/releases/download/v2026.02.01/SHA256SUMS
grep '  4vrs-cf-wizard-v2026.02.01-ubuntu24-docs-r2.zip$' SHA256SUMS > wizard.SHA256SUMS
test -s wizard.SHA256SUMS
sha256sum -c wizard.SHA256SUMS
```

## Порядок действий

1. На Ubuntu 24.04 x86-64 сверить SHA256 выдаваемого ZIP. Распаковать штатным unzip в новый root-owned каталог на PC (не на CF):

   ```sh
   sudo install -d -m 0755 /opt/4vrs-v2026.02.01-final
   sudo unzip 4vrs-cf-wizard-v2026.02.01-ubuntu24-docs-r2.zip -d /opt/4vrs-v2026.02.01-final
   cd /opt/4vrs-v2026.02.01-final
   sha256sum -c SHA256SUMS
   sudo python3 cf-wizard.py --list
   sudo python3 cf-wizard.py --report /root/cf-v2026.02.01-new.json
   ```

   Выбрать и проверить физическую карту, режим полной очистки только для новой/разрешённой запасной CF и MAC целевой Moxa; подтвердить очистку в мастере. UUID после ext3 прочитать на PC напрямую и сверить с отчётом. Не переносить RNG со старой карты; старую рабочую CF сохранить отдельно. Исторический Bash UUID не применять как единственное подтверждение. Worker проверяет подготовленные файлы; итог — commissioned=false, первичная активация ожидается.

После подготовки мастер размонтирует CF. Следующий шаг со свежим входом нужен только для новой карты, не для обновления активной schema 3. Для новой карты перед созданием входа заново найдите именно её раздел ext3 и сверьте UUID с отчётом мастера. Если `/mnt/4vrs-cf` уже занят, не монтируйте поверх него.

```sh
lsblk -o NAME,SIZE,FSTYPE,UUID,MOUNTPOINTS,MODEL,SERIAL
read -r -p 'CF partition path from lsblk: ' cf_partition
sudo blkid "$cf_partition"
sudo install -d -m 0700 /mnt/4vrs-cf
sudo mount -o nosuid,nodev "$cf_partition" /mnt/4vrs-cf
findmnt /mnt/4vrs-cf
```


2. Для новой schema1 требуется **отдельный свежий вход** ровно 32 байта от CSPRNG доверенного Ubuntu PC. Он не входит в ZIP/отчёт. После окончания мастера смонтировать ту же проверенную CF на PC в root-owned `/mnt/4vrs-cf` с nosuid,nodev, проверив её UUID и устройство через findmnt/blkid. Каждая новая файловая система имеет собственный UUID. Создайте одноразовый вход:

   ```sh
   sudo python3 - <<'PY'
   import os
   from pathlib import Path
   root=Path('/mnt/4vrs-cf')  # сначала вручную подтвердить mount/UUID
   seed=os.getrandom(32, os.GRND_NONBLOCK)
   if len(seed)!=32: raise RuntimeError('short entropy; stop')
   service=root/'4vrs-service'
   os.mkdir(service, 0o700)  # существующий каталог не переиспользовать вслепую
   d=os.open(service, os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
   fd=os.open('fresh-rng-input',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=d)
   try:
       if os.write(fd,seed)!=32: raise RuntimeError('short write; stop')
       os.fsync(fd)
   finally: os.close(fd)
   os.fsync(d); os.close(d)
   parent=os.open(root,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
   os.fsync(parent);os.close(parent)
   PY
   ```

   При ошибке прекратить процедуру, не повторять вход и не считать частичный файл пригодным. Штатно размонтировать карту, затем физически доставить её в выключенный прибор. Не передавать вход через старый SSH, аргументы, clipboard, shell history или журнал. Unlink после использования не гарантирует физического стирания NAND; защищать сам носитель.

После успешного создания файла:

```sh
sync
sudo umount /mnt/4vrs-cf
findmnt /mnt/4vrs-cf
```

Последняя команда должна не показать монтирование (код выхода 1 для отсутствующего mount). Только после успешного umount извлеките карту. Вставляйте её в выключенный прибор.

3. На приборе выбрать каталог `4vrs-packages/gateway-<первые 16 символов SHA256 gateway.tar.gz>` по новому SHA256SUMS. Проверить MAC/UUID и конфигурацию. Выполнить один `./4vrs-install`. Операция асинхронна: опрашивать `./4vrs-install --status` до конечного результата; **result100 — ещё работа**. Result0 verify-installed означает завершённую установку, result3 already-installed — no-op. Другие результаты разбирать, не повторять установку по тайм-ауту три секунды. До активации Web может быть закрыт из-за отсутствующей RNG policy.

На Moxa войдите как root через консоль либо уже настроенный SSH. Не используйте пароль другого прибора. Убедитесь по `mount`, что `/var/hda` — смонтированная CF, а не пустой каталог во внутренней памяти; MAC `eth0` должен совпадать с выбранным в мастере. Для опубликованного установочного архива путь пакета следующий:

```sh
id
kversion
ifconfig eth0
mount
df -k /var/hda /etc
ls -ld /var/hda/4vrs-packages/gateway-624f807edb127977
cd /var/hda/4vrs-packages/gateway-624f807edb127977
./4vrs-install
./4vrs-install --status
```

Повторяйте только `./4vrs-install --status`, пока `result=100`; не запускайте установку повторно. Если связь прервалась, после входа используйте `/etc/4vrs-installer/recovery --status`. `result=0` или `result=3` — успешный конечный результат; другой результат требует разбора.

4. После terminal result согласованно остановить приложение штатной командой `/etc/init.d/4vrs-gateway stop`, убедиться в завершении владельца RNG. Для новой согласованной schema1 выполнить **однократно**:


```sh
/etc/init.d/4vrs-gateway stop
ps
```

Дождитесь исчезновения процессов `4vrs-gateway` и `4vrs-rng`; не продолжайте при живом владельце RNG. Далее выполните приведённую ниже production-enable только для новой schema 1.

   ```sh
   /var/hda/4vrs/bin/4vrs-rng production-enable /var/hda/4vrs-rng --fresh-entropy-confirmed < /var/hda/4vrs-service/fresh-rng-input
   ```

   При ненулевом результате остановиться; service-recover не является автоматическим fallback. После успеха проверить `4vrs-rng status /var/hda/4vrs-rng`: ready/schema3. Удалить только использованный `/var/hda/4vrs-service/fresh-rng-input`, завершить запись файловой системы и не переиспользовать вход. RNG state/witness/маркеры не удалять. Для уже активной исправной schema3 эта операция **не нужна**.


```sh
/var/hda/4vrs/bin/4vrs-rng status /var/hda/4vrs-rng
```

Только если активация завершилась успешно и статус подтверждает готовую schema 3:

```sh
rm /var/hda/4vrs-service/fresh-rng-input
sync
```

5. Один штатный `/etc/init.d/4vrs-gateway start`; проверить Gateway/RNG/Web, поколение, HTTP200 на настроенном адресе и TCP P1/P2. Если требуется HTTPS/авторизация, проверить их отдельно. Проверить неизменность gateway.conf конфиденциальным способом, без экспорта содержимого. Ready RNG не равен готовности всей системы, а TCP-соединение не доказывает отсутствие потерь Modbus. Дальнейшие обычные рестарты при согласованной CF автономны.

На запасной CF оператор подтвердил именно отдельные подготовку, первичную активацию, установку и проверку; это не доказательство автоматического выполнения всех шагов мастером. Для воспроизводимого процесса выше порядок установки и явного перехода отделён от обычного runtime. По уже введённой карте активацию повторять не требуется.

## Проверка после установки

```sh
/etc/init.d/4vrs-gateway start
ps
/var/hda/4vrs/bin/4vrs-rng status /var/hda/4vrs-rng
```

Запустите Gateway один раз. На LCD проверьте готовность нужных портов и System → Web Server → Running. F5 URLs показывает адрес для браузера. На новой установке используется HTTP. Для первого администратора выберите New code и задайте собственный пароль в браузере. Проверьте чтение реального прибора через используемый Modbus-клиент. Успех Web и открытый TCP-порт не заменяют проверку данных. См. [руководство](user-guide.ru.md).

При остановленной установке, ошибке RNG или отсутствии CF не удаляйте state/witness/маркеры и не повторяйте production-enable вслепую. Сохраните несекретный статус установщика/RNG и используйте [диагностику](rng-autonomous-v1.ru.md).
