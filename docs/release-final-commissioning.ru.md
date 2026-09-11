# Первичный ввод: CF → установка → явная активация → Web

[English](release-final-commissioning.md) · [Матрица](release-final.ru.md)

Это инструкция для отдельно согласованного окна обслуживания, не выполненная здесь операция. Цель — только Moxa #2 192.0.2.15:22, MAC 02:00:00:00:00:02; на другой прибор автоматически не переходить. Мастер готовит карту, но **не выполняет полностью автоматический ввод**. Объединение установки и первичной активации требует отдельной разработки; скрытая регенерация не добавлена.

1. На Ubuntu 24.04 x86-64 сверить SHA256 выдаваемого ZIP. Распаковать штатным unzip в новый root-owned каталог на PC (не на CF):

   ```sh
   sudo install -d -m 0755 /opt/4vrs-v2026.02.01-final
   sudo unzip 4vrs-cf-wizard-v2026.02.01-final-review.zip -d /opt/4vrs-v2026.02.01-final
   cd /opt/4vrs-v2026.02.01-final
   sha256sum -c SHA256SUMS
   sudo python3 cf-wizard.py --list
   sudo python3 cf-wizard.py --report /root/cf-v2026.02.01-new.json
   ```

   Выбрать и проверить физическую карту, режим полной очистки только для новой/разрешённой запасной CF и MAC целевой Moxa; подтвердить очистку в мастере. UUID после ext3 прочитать на PC напрямую и сверить с отчётом. Не переносить RNG со старой карты; старую рабочую CF сохранить отдельно. Исторический Bash UUID не применять как единственное подтверждение. Worker проверяет подготовленные файлы; итог — commissioned=false, первичная активация ожидается.

2. Для новой schema1 требуется **отдельный свежий вход** ровно 32 байта от CSPRNG доверенного Ubuntu PC. Он не входит в ZIP/отчёт. После окончания мастера смонтировать ту же проверенную CF на PC в root-owned `/mnt/4vrs-cf` с nosuid,nodev, проверив её UUID и устройство через findmnt/blkid. Для уже подтверждённой запасной карты UUID d4a46714-7f07-4fbf-ae88-4fb6e709512b, но для другой новой карты использовать её фактический UUID. Пример создания одноразового входа (не выполнялся в этой работе):

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

3. На приборе выбрать каталог `4vrs-packages/gateway-<первые 16 символов SHA256 gateway.tar.gz>` по новому SHA256SUMS. Проверить MAC/UUID и конфигурацию. Выполнить один `./4vrs-install`. Операция асинхронна: опрашивать `./4vrs-install --status` до конечного результата; **result100 — ещё работа**. Result0 verify-installed означает завершённую установку, result3 already-installed — no-op. Другие результаты разбирать, не повторять установку по тайм-ауту три секунды. До активации Web может быть закрыт из-за отсутствующей RNG policy.

4. После terminal result согласованно остановить приложение штатной командой `/etc/init.d/4vrs-gateway stop`, убедиться в завершении владельца RNG. Для новой согласованной schema1 выполнить **однократно**:

   ```sh
   /var/hda/4vrs/bin/4vrs-rng production-enable /var/hda/4vrs-rng --fresh-entropy-confirmed < /var/hda/4vrs-service/fresh-rng-input
   ```

   При ненулевом результате остановиться; service-recover не является автоматическим fallback. После успеха проверить `4vrs-rng status /var/hda/4vrs-rng`: ready/schema3. Удалить только использованный `/var/hda/4vrs-service/fresh-rng-input`, завершить запись файловой системы и не переиспользовать вход. RNG state/witness/маркеры не удалять. Для уже активной исправной schema3 эта операция **не нужна**.

5. Один штатный `/etc/init.d/4vrs-gateway start`; проверить Gateway/RNG/Web, поколение, HTTP200 на настроенном адресе и TCP P1/P2. Если требуется HTTPS/авторизация, проверить их отдельно. Проверить неизменность gateway.conf конфиденциальным способом, без экспорта содержимого. Ready RNG не равен готовности всей системы, а TCP-соединение не доказывает отсутствие потерь Modbus. Дальнейшие обычные рестарты при согласованной CF автономны.

На запасной CF оператор подтвердил именно отдельные подготовку, первичную активацию, установку и проверку; это не доказательство автоматического выполнения всех шагов мастером. Для воспроизводимого процесса выше порядок установки и явного перехода отделён от обычного runtime. По уже введённой карте активацию повторять не требуется.
