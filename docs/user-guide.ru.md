# 4VRS Gateway v2026.02.03 — Инструкция пользователя

[README](../README.ru.md) · [LCD / Web](screens.ru.md) · [Release scope](release-final.ru.md)

## Содержание

- [Требование к заводской прошивке](#требование-к-заводской-прошивке)
- [Установка](#установка)
- [Дерево меню](#дерево-меню)
- [Дерево Web-панели](#дерево-web-панели)
- [Эксплуатация и навигация](#эксплуатация-и-навигация)
- [Главный экран](#главный-экран)
- [Последовательные порты и транспорты](#последовательные-порты-и-транспорты)
- [Настройки сети](#настройки-сети)
- [Часы и NTP](#часы-и-ntp)
- [Диагностика и остановка](#диагностика-и-остановка)
- [Быстрая проверка проблем](#быстрая-проверка-проблем)
- [Web на UC-7420-LX Plus](#web-на-uc-7420-lx-plus)
- [LCD — подсветка и Web Server](#lcd--подсветка-и-web-server)
- [Web — страницы и действия](#web--страницы-и-действия)

## Требование к заводской прошивке

Поддерживаются UC-7420-LX Plus с ОС 1.6 / Linux 2.6.10 и UC-7420-LX без Plus с ОС 2.3 / Linux 2.4.18. Универсальный установщик выбирает профиль на приборе; выбирать ОС в мастере CF не нужно. Другие модели не квалифицированы.

Ниже приведены требования к заводской прошивке **Plus**.

Перед установкой 4VRS Gateway на **Moxa UC-7420-LX Plus** обновите заводскую
прошивку до **1.6** (`FWR_UC7400P_V1.6_Build_09110414`). Это обязательная
базовая версия для установки приложения по данной инструкции.

Проверьте на приборе:

```sh
kversion
```

Ожидаемый ответ:

```text
UC-7420-LX Plus firmware version 1.6
```

На дату проверки 2026-09-10 версия 1.6 — последняя опубликованная готовая
Linux 2.6.x-прошивка для этой платформы в [каталоге Moxa](https://www.moxa.com/en/products/phased-out-products/uc-7410%2C-uc-7420-series).
Указанная там версия 2.3 относится к Linux 2.4.x; не выбирайте образ только по
большему номеру версии. Образ должен соответствовать модели **LX Plus**.

Обновление заводской прошивки выполняется **до установки Gateway**, отдельно
от мастера CF и установщика приложения. Оно стирает данные и настройки внутренней
Flash: заранее сохраните нужные настройки и обеспечьте доступ через консоль.
Во время прошивки не отключайте питание. После перезагрузки повторите `kversion`
и проверьте сеть по [руководству производителя](https://www.moxa.com/Moxa/media/PDIM/S100000489/moxa-uc-7410-lx-plus-series-software-manual-v6.0.pdf),
раздел Upgrading the Firmware, страницы 3-4–3-5.

## Установка

Полный пакет и мастер CF доступны в [релизе](https://github.com/dk-1983/moxa-4vrs-gateway/releases/tag/v2026.02.03). Используйте [актуальный порядок ввода в эксплуатацию](release-final-commissioning.ru.md), включая явную первичную активацию RNG. Старые инструкции ручной установки не относятся к этому выпуску.

## Дерево меню

Названия пунктов ниже совпадают с надписями на дисплее. В обычном списке
**F2/F4** перемещают выбор, **F3** открывает пункт, **F1** возвращает назад.
Доступные действия редактора всегда указаны внизу экрана.

```text
Главный экран
├── F1 Help — справка
└── F3 Main Menu
    ├── Status — состояние приложения
    ├── Ports
    │   └── P1 … P8 → F3 Detail
    │       └── F2/F4: подключение → счётчики → ошибки → состояние порта
    ├── Configuration
    │   ├── P1 … P8 → F3 Select
    │   │   ├── Параметры порта → F5 Edit
    │   │   ├── Save & Apply → подтверждение → результат
    │   │   └── Cancel changes
    │   └── F5 Network
    │       ├── F2/F4: LAN2 / LAN1 / маршрут / DNS / Observed LAN1 / Observed LAN2
    │       └── F5 Edit → F5 Review → F3 Apply
    │           ├── F3 Keep — сохранить
    │           └── F1 Revert / истечение таймера — откатить
    ├── Diagnostics
    │   └── F3 Events → Startup Events → F2/F4: записи
    ├── System — F2/F4: Date & Time ↔ Platform ↔ Display ↔ Web Server
    │   ├── Date & Time → F5 Set Time → F3 Next → confirmation
    │   ├── Platform → F5 Network Time
    │   │   ├── F5 Edit: enable / server / interval → confirmation
    │   │   └── F4 Test / Stop
    │   ├── Display → F3 Open → Backlight → F3 Open
    │   │   └── F2/F4 On/Off → F3 Save
    │   └── Web Server → F3 Open
    │       ├── Enable / Disable → F3 Set
    │       ├── Iface LAN1 / LAN2 / Both → F3 Set
    │       ├── New code → F3 Set → One-time code
    │       ├── Recover access → F3 Set → F3 Yes / F1 No
    │       ├── HTTP cleartext / HTTPS not rec. → F3 Set
    │       └── F5 URLs → [HTTPS only] F5 Cert → SHA-256
    ├── Shutdown → F3 Yes / F1 Cancel
    │   ├── Stopping Gateway → Gateway stopped
    │   └── Shutdown failed → F1 Back → Main Menu
    └── About — название и версия
```



System открывается на Date & Time. F4 последовательно переключает Platform → Display → Web Server → Date & Time; F2 идёт в обратном направлении. Поэтому из Date & Time можно сразу попасть на Web Server кнопкой F2. Display и Web Server открываются F3; Network Time открывается F5 только на Platform. Названия дерева сохранены на языке LCD.

## Дерево Web-панели

```text
Web
├── Administrator sign in / initial enrollment / access recovery
└── Signed in
    ├── Overview → Refresh / Diagnostics
    ├── Ports → P1 … P8 → Edit → Save
    ├── Network → Review → Apply → Keep / Revert
    ├── Diagnostics → Startup events / P1 … P8 Details
    ├── System
    │   ├── Web Server → State / Interface / Protocol → Save
    │   ├── Backlight → Save
    │   ├── NTP → Enabled / Server / Interval → Save
    │   ├── Manual time → Save
    │   ├── Test NTP / Stop NTP test
    │   ├── Security → Change password → Save
    │   ├── Stop Gateway → confirmation
    │   └── [HTTPS only] TLS / HTTPS → status / certificate SHA-256
    ├── Help
    └── About
```

## Эксплуатация и навигация

## Главный экран

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/home.png" alt="Главный экран и физические кнопки" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre><b>▶ Главный экран</b></pre>
</td>
</tr>
</table>

*Изображение главного экрана.*

Используйте существующий сетевой адрес Moxa. При миграции сохраняется её
собственная конфигурация: не копируйте сетевой профиль с другого устройства.

На главном экране **F3** открывает меню, **F1** — справку. В меню находятся
Status, Ports, Configuration, Diagnostics, System, Shutdown и About.
Внизу экрана указаны доступные кнопки. Обычно F2/F4 перемещают выбор,
F3 выбирает пункт, F1 возвращает назад.

`READY` и `Ports … ready` означают готовность приложения и портов, но не
доказывают ответ прибора. Для этого нужен успешный запрос программы-клиента.
`Clients 0` означает отсутствие учитываемых клиентов в данный момент.

### Главное меню

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/main-menu.png" alt="Главное меню, выбран Status" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── <b>▶ F3 → Main Menu</b></pre>
</td>
</tr>
</table>

*Главное меню.*

F2/F4 перемещают выбор, F3 открывает пункт, F1 возвращает на главный экран.
Status показывает состояние приложения, Ports — состояния и счётчики портов,
Configuration — настройки, Diagnostics — диагностику и события запуска,
System — часы и платформу, About — версию. Shutdown останавливает только приложение.

## Последовательные порты и транспорты

Откройте **Configuration**, выберите физический порт и проверьте его параметры.
Режим линии, скорость, биты данных, чётность и стоп-биты должны соответствовать
прибору. P1–P8 закреплены за физическими UART. Разъём RJ45 сам по себе не
означает Ethernet: подключение проверяйте по документации устройства.

Выберите поле и используйте указанную на экране кнопку **F5 Edit**. F2/F4
изменяют значение; подтвердите его по подсказке редактора. Затем выполните
**Save & Apply** конфигурации порта и подтверждение.
**Cancel changes** отменяет черновик.
Одно редактирование поля ещё не означает сохранение всей конфигурации.

После подтверждения Apply дождитесь **Config Result**. `APPLYING...` означает,
что операция продолжается. Обычный `APPLIED` означает успешную транзакцию,
включая сохранение; `NO CHANGES` — отсутствие изменений. `INVALID / NOT APPLIED`
отклоняет черновик. `FAILED / ROLLED BACK` сообщает об отказе изменения и откате;
`ROLLBACK FAILED` требует диагностики. Если рядом с `APPLIED` указано
`DURABILITY?`, надёжность сохранения не подтверждена: нельзя считать, что
настройка гарантированно переживёт отключение питания. Запишите результат
и проверьте накопитель.

| Транспорт на панели | Формат и назначение |
| --- | --- |
| MB TCP | Modbus TCP с заголовком MBAP |
| MB UDP | Modbus с заголовком MBAP в UDP |
| RTU/UDP | Полные кадры Modbus RTU, включая CRC, в UDP |
| RAW TCP | Прозрачный поток байтов TCP ↔ последовательный порт |
| RAW UDP | Прозрачный обмен UDP ↔ последовательный порт |

В режимах Modbus последовательная сторона использует RTU. Адрес прибора —
это unit address в запросе клиента; IP самой Moxa настраивается отдельно.
RAW не преобразует произвольные данные в запросы Modbus. На клиенте и шлюзе
нужно выбрать соответствующие друг другу транспорты.

Новая конфигурация: все восемь портов включены, RS-232, 9600, 8N1, Modbus TCP,
сетевые порты 502–509. Сохранённая конфигурация может отличаться.
Выберите подходящий адрес привязки слушателя и свободный сетевой порт.
`0.0.0.0` — привязка ко всем подходящим локальным адресам, а не адрес назначения
для программы-клиента.

В разделе **Ports** выберите порт и листайте его состояние, счётчики и ошибки
кнопками F2/F4. TCP-подключение подтверждает доступ к сокету. Для проверки всей
цепочки нужен корректный ответ подключённого прибора.

### Просмотр портов

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/ports.png" alt="Список портов, выбран P1" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── <b>▶ F3 → Ports</b></pre>
</td>
</tr>
</table>

*Список портов.*

F2/F4 перебирают все восемь портов, при необходимости прокручивая список
из пяти видимых строк. F3 Detail открывает выбранный порт; F2/F4 затем
переключают страницы подробностей. F1 возвращает к списку. Это просмотр
состояния, а не редактирование конфигурации.

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/port-details.png" alt="Первая страница подробностей порта" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── F3 → Ports
      └── <b>▶ F3 → P1 → Detail</b></pre>
</td>
</tr>
</table>

*Параметры подключения порта.*

В примере `115200 8NONE1` означает 115200 бод, 8 бит данных, без чётности,
1 стоп-бит. `TCP 1502` — порт слушателя, а не адрес прибора Modbus.

Заголовок подробностей содержит номер физического порта и страницы, например
`Port 1: 1/4`. Всего четыре страницы:

| Страница | Поля |
| --- | --- |
| 1/4 — Подключение | UART (`ttyM0` — P1), режим линии, скорость/формат, транспорт, адрес привязки, TCP/UDP-порт и клиенты |
| 2/4 — Транзакции | Accepted, Complete, Timeout, Recovery, текущая Queue и максимальная наблюдавшаяся High water |
| 3/4 — Протокол | Ошибки CRC и Frame, несовпадения Unit/Function, начальный мусор Garbage и устаревшие ответы Stale |
| 4/4 — Работа порта | Поколение конфигурации Generation, Last ok и текст ошибки |

`Queue` — текущая глубина очереди, `High water` — её зафиксированный максимум.
Счётчики относятся к работе выбранного порта/транспорта, а не к показаниям
водосчётчика или сохраняемым итогам прибора. Диагностику Modbus нужно
интерпретировать с учётом выбранного транспорта. `Last ok` — временная метка
работы порта, а не календарная дата. Знак `!` рядом с портом в списке отмечает
зарегистрированную ошибку; её причину выясняйте по подробностям.

## Настройки сети

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/configuration.png" alt="Вход в настройки" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── <b>▶ F3 → Configuration</b></pre>
</td>
</tr>
</table>

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/network-lan2.png" alt="Подтверждённые настройки LAN2" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── F3 → Configuration
      └── F5 → Network
         └── <b>▶ LAN2</b></pre>
</td>
</tr>
</table>

*Меню конфигурации и сетевые настройки LAN2.*

Главное меню → **Configuration** → **F5 Network** на списке портов.
F2/F4 переключают страницы LAN2, LAN1, Default route, Global DNS, Observed LAN1
и Observed LAN2. На странице настроек нажмите F5 Edit.

**Confirmed policy** показывает принятую и сохранённую политику.
**Observed** показывает текущее состояние интерфейса. При DHCP это особенно
важное различие. `Link down` означает, что сейчас интерфейс сообщает отсутствие
линка; это не сообщение об удалении сохранённого IP.

У LAN редактируются октеты IP, октеты маски и режим Static/DHCP. F3 переходит
к следующему полю, F2/F4 меняют значение. Выбирайте адреса своей сети.
DHCP работает как клиент: Moxa не раздаёт адреса. Резервирование адреса по MAC
настраивается отдельно на DHCP-сервере, например на маршрутизаторе.

Default route выбирает отсутствие основного маршрута, LAN1 или LAN2 и адрес
шлюза. Global DNS выбирает ручные серверы или автоматический DNS от одного
выбранного DHCP-интерфейса. Источник DNS и интерфейс основного маршрута — разные
настройки. Просроченная DHCP-аренда не превращается в постоянный статический IP.

### Apply, Keep и Revert

1. Завершите редактирование и нажмите **F5 Review**.
2. Проверьте новые значения. Нажмите **F3 Apply** для временного применения.
3. Пока идёт отсчёт подтверждения, проверьте доступ по новому адресу.
4. Нажмите **F3 Keep**, чтобы принять и сохранить изменения. Результат — `KEPT`.
5. Для отмены нажмите **F1 Revert** либо дождитесь истечения таймера, не нажимая
   Keep. Успешный откат завершается надписью `REVERTED`.

Окно подтверждения — 60 секунд. **Keep сохраняет, Revert отменяет.** Перед
нажатием читайте подсказки текущего экрана. До Apply кнопка F1 Cancel отменяет
редактирование: это другая стадия, чем откат уже применённой сети.

Keep завершает сетевую транзакцию: отдельного второго Save для неё нет.
После успешного Keep подтверждённая политика должна сохраняться при перезапуске.
При ошибке Apply или отката запишите сообщение и используйте доступный канал
управления для диагностики, а не повторяйте изменения вслепую.
При возврате к DHCP отсутствие сервера может оставить интерфейс в ожидании
новой аренды — восстановление политики не восстанавливает просроченную аренду.

## Часы и NTP

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/network-time.png" alt="Настройки Network Time и состояние синхронизации" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── F3 → System
      └── F4 → Platform
         └── <b>▶ F5 → Network Time</b></pre>
</td>
</tr>
</table>

Откройте **System**, перейдите на страницу Platform и откройте **Network Time**
по подсказкам дисплея. Измените сервер, включение NTP и интервал, затем
подтвердите. Штатные интервалы — **1, 6 и 24 часа**, по умолчанию **1 час**.
Сервер должен быть доступен; для имени сервера дополнительно нужен рабочий DNS.

При включённом NTP **F4 Test** запускает диагностику с минутным интервалом.
Она автоматически заканчивается после **трёх попыток**, затем возвращается
к сохранённому штатному интервалу. Три попытки не обязательно означают три
успешных ответа. Кнопка Stop завершает диагностику досрочно.

Главный экран может показывать `TIME UNSYNC`, `TIME SYNCING` или `TIME MANUAL`.
После синхронизации предупреждение исчезает; явное состояние смотрите в System.
Работоспособность RTC и успешная синхронизация NTP — отдельные факты.
Одна надпись `TIME UNSYNC` не доказывает неисправность батареи. Проверьте
включение NTP, сервер, линк, маршрут, DNS и результат диагностики часов.

Сервис часов использует RTC в UTC. Сохраните настройки NTP и проверьте
автоматическую синхронизацию после установки или согласованного перезапуска.

## Диагностика и остановка

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/diagnostics.png" alt="Счётчики диагностики" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── <b>▶ F3 → Diagnostics</b></pre>
</td>
</tr>
</table>

*Счётчики диагностики.*

Откройте **Main Menu → Diagnostics**, чтобы посмотреть число принятых и
завершённых запросов, тайм-аутов, восстановлений, устаревших ответов и
максимальную глубину очереди. **F3 Events** открывает события запуска.

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/startup-events.png" alt="События запуска" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── F3 → Diagnostics
      └── <b>▶ F3 → Startup Events</b></pre>
</td>
</tr>
</table>

*События запуска.*

**F2/F4** переключают записи. В каждой показаны порядковый номер, этап запуска,
прогресс, результат и ошибка. `BOOT / Progress 0%` относится к выбранному
событию запуска, а не к текущей готовности приложения.

В **Diagnostics** доступны события запуска, в **About** — версия продукта.
При сообщении об ошибке запишите модель, версию, порт, транспорт, параметры
линии, выполненное действие и точный результат. Уберите из публикуемых журналов
учётные данные и частные сведения об инфраструктуре.

**Shutdown останавливает приложение Gateway. Он не выключает питание и не
перезагружает ОС Moxa.** Перезапуск ОС и физическое отключение выполняются
отдельно. Не запускайте второй Gateway на тех же UART.

После подтверждения виден ход остановки и итоговый экран. `Gateway stopped` означает завершение приложения; ОС ещё работает. При `Shutdown failed` показана причина, F1 возвращает в меню. Возврат не запускает уже остановленные порты. [Экран и подробности](releases/v2026.02.03-lcd.ru.md).

## Быстрая проверка проблем

| Симптом | Что проверить сначала |
| --- | --- |
| Нет связи после переноса прибора | IP/порт Moxa в клиенте и фактическое подключение линии |
| TCP подключается, данных нет | Адрес прибора, режим/скорость/чётность, транспорт и состояние прибора |
| Сеть вернулась к старым значениям | Нажат ли Keep до истечения таймера? Какой результат или ошибка на экране? |
| Confirmed содержит IP, но Link down | Кабель, порт коммутатора и соответствующий физический LAN |
| DHCP ожидает сеть | Линк и сервер DHCP; старая аренда могла истечь |
| Время не синхронизировано | Включение NTP, сервер, маршрут, DNS и результат диагностики |

Описание Web относится к релизу `v2026.02.01`.
Автоматическое обновление остаётся отдельной будущей функцией.

## Web на UC-7420-LX Plus

HTTPS не рекомендуется для данного прибора из-за дополнительных вычислительных затрат и задержек подключения. Проверка пароля при авторизации создаёт временную высокую нагрузку — в испытаниях около **92% CPU**, независимо от протокола.

После оптимизации обычная работа Web-панели создаёт небольшую нагрузку: в пользовательском HTTP-тесте процесс Web потреблял в среднем около **0,06% CPU**, без учёта отдельного процесса проверки пароля. После входа интерфейс работает отзывчиво в обоих режимах.

HTTP рекомендуется для выделенной доверенной сети управления и остаётся режимом по умолчанию новой установки. HTTP передаёт пароль и данные без шифрования. HTTPS (TLS) остаётся доступной, но не рекомендуемой опцией на Moxa UC-7420-LX Plus. Обновление сохраняет явный выбор HTTP/HTTPS; конфигурация schema 1/2 без поля протокола сохраняет прежний HTTPS. Ограничение одним клиентом и хеширование пароля не защищают передаваемые по HTTP данные.

[Measurement conditions / условия измерений](release-final.ru.md).

## LCD — подсветка и Web Server

<table><tr><td><a href="../assets/images/menu/system-display.png"><img src="../assets/images/menu/system-display.png" alt="system-display" width="360"></a></td><td><pre>Main Menu
└─ System
   └─ Display (F4 × 2) ←</pre></td></tr></table>

<table><tr><td><a href="../assets/images/menu/display-menu.png"><img src="../assets/images/menu/display-menu.png" alt="display-menu" width="360"></a></td><td><pre>Main Menu
└─ System
   └─ Display (F4 × 2)
      └─ F3 Open → Display menu ←</pre></td></tr></table>

<table><tr><td><a href="../assets/images/menu/backlight-on.png"><img src="../assets/images/menu/backlight-on.png" alt="backlight-on" width="360"></a></td><td><pre>Main Menu
└─ System
   └─ Display (F4 × 2, F3 Open)
      └─ Backlight (F3 Open) → On ←</pre></td></tr></table>

<table><tr><td><a href="../assets/images/menu/web/web-system-menu.png"><img src="../assets/images/menu/web/web-system-menu.png" alt="web-system-menu" width="360"></a></td><td><pre>Main Menu
└─ System
   └─ Web Server (F2 from Date & Time) ←</pre></td></tr></table>

<table><tr><td><a href="../assets/images/menu/web/web-server-http.png"><img src="../assets/images/menu/web/web-server-http.png" alt="web-server-http" width="360"></a></td><td><pre>Main Menu
└─ System
   └─ Web Server (F2, F3 Open)
      └─ HTTP cleartext ←</pre></td></tr></table>

<table><tr><td><a href="../assets/images/menu/web/web-https-addresses.png"><img src="../assets/images/menu/web/web-https-addresses.png" alt="web-https-addresses" width="360"></a></td><td><pre>Main Menu
└─ System
   └─ Web Server (F2, F3 Open)
      └─ URLs (F5) ←</pre></td></tr></table>

<table><tr><td><a href="../assets/images/menu/web/web-certificate.png"><img src="../assets/images/menu/web/web-certificate.png" alt="web-certificate" width="360"></a></td><td><pre>Main Menu
└─ System
   └─ Web Server (F2, F3 Open)
      └─ URLs (F5)
         └─ Cert (F5) ←</pre></td></tr></table>

На странице Display нажмите F3, затем F3 на Backlight; F2/F4 выбирают On/Off, F3 сохраняет. Stored — сохранённый выбор, Command — результат команды подсветке.

В Web Server F2/F4 выбирают одну из пяти строк. Enable/Disable — действие, поэтому надпись Disable при Running означает, что сервер включён. F3 меняет выбранное включение, интерфейс или протокол и запускает сохранение. Дождитесь завершения Saving…; повторное переключение не требует выхода через F5/F1. При Save failed или Config conflict проверьте результат и откройте страницу заново. New code выдаёт одноразовый код, Recover access запрашивает подтверждение. F5 открывает адреса; Unavailable означает отсутствие доступного URL на данном интерфейсе. В HTTPS F5 Cert показывает отпечаток: сравните все 64 знака SHA-256 с сертификатом браузера. F1 возвращает к Web Server.

## Web — страницы и действия

Один Web TCP-сеанс обслуживается одновременно; дополнительные TCP-соединения закрываются до TLS и не вытесняют действующее. Это ограничение соединений, а не гарантия одной вкладки или одного пользователя. При отказе подключения закройте лишние подключения и повторите после освобождения текущего.

### Вход и восстановление доступа

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-sign-in-security-help-ru.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-sign-in-security-help-ru.png" alt="web-sign-in-security-help-ru" width="600"></a></td><td><pre>Web
└─ Вход администратора
   └─ Справка о соединении ←</pre></td></tr></table>

Первый администратор создаётся по одноразовому коду с LCD и вашему новому паролю. Код действует три минуты, допускается не более пяти попыток. Для нового кода откройте LCD System → Web Server → New code. Последующий вход требует только пароля. Пароль занимает 12–128 байт UTF-8; это не всегда 12–128 символов. Глаз внутри каждого поля показывает или скрывает только это поле. «Забыли пароль?» запрашивает подтверждение на приборе: F3 подтверждает восстановление, затем введите код с дисплея и новый пароль. Это восстановление Web-доступа, а не сброс конфигурации портов.

### Обзор — Overview

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-overview-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-overview-en.png" alt="web-overview-en" width="600"></a></td><td><pre>Web
└─ Overview ←</pre></td></tr></table>

Показывает готовность портов, TCP-соединения, аварии портов, состояние Web, часов и время работы ОС. Refresh обновляет данные, Diagnostics открывает диагностику. Следите за отметкой обновления: устаревший снимок не доказывает текущее состояние. Готовность порта и TCP-соединение сами по себе не подтверждают успешный ответ датчика.

### Порты — Ports

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-ports-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-ports-en.png" alt="web-ports-en" width="600"></a></td><td><pre>Web
└─ Ports ←</pre></td></tr></table>

В списке P1–P8 указаны включение, UART, транспорт и адрес прослушивания. Edit открывает настройки выбранного физического порта. Disabled не означает отсутствующий аппаратный порт. Адрес 0.0.0.0 означает прослушивание всех подходящих локальных адресов; клиент подключается к реальному IP прибора.

### Настройка порта

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-port-p1-settings-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-port-p1-settings-en.png" alt="web-port-p1-settings-en" width="600"></a></td><td><pre>Web
└─ Ports
   └─ P1 → Edit ←</pre></td></tr></table>

Настройте State, Serial mode, Baud, Data bits, Parity, Stop bits, Transport, Bind IP, TCP/UDP port и при необходимости Special baud. Изменения вступают в силу после Save. Проверьте результат и фактический обмен: изменение UART или транспорта может прервать текущие соединения. При конфликте с настройкой из LCD откройте форму заново и повторно проверьте значения.

### Сеть — Network

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-network-settings-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-network-settings-en.png" alt="web-network-settings-en" width="600"></a></td><td><pre>Web
└─ Network ←</pre></td></tr></table>

LAN1 и LAN2 настраиваются отдельно: Static/DHCP, IP, маска и шлюз. Default route выбирает интерфейс основного маршрута; DNS задаёт серверы вручную либо источник DHCP. Observed отражает наблюдаемое состояние, а не только сохранённую настройку. Review показывает общий проект изменений и ничего не применяет. Apply запускает временное применение; при смене адреса подключитесь к новому. Keep подтверждает сохранение в течение 60 секунд. Revert или истечение времени возвращает предыдущую сеть. Не запускайте новый проект сети, пока предыдущая операция ожидает подтверждения.

### Диагностика — события запуска

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-startup-events-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-startup-events-en.png" alt="web-diagnostics-startup-events-en" width="600"></a></td><td><pre>Web
└─ Diagnostics
   └─ Startup Events ←</pre></td></tr></table>

Таблица содержит историю этапов запуска, а не текущий процент загрузки. Прочерк в Ports означает, что событие не привязано к порту; в Error — отсутствие указанной ошибки. Это не потерянные счётчики. Строки Starting остаются историческими событиями даже после Ready / Completed / 100%. Кнопки P1–P8 открывают детали портов.

### Диагностика порта

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-port-p1-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-port-p1-en.png" alt="web-diagnostics-port-p1-en" width="600"></a></td><td><pre>Web
└─ Diagnostics
   └─ P1 ←</pre></td></tr></table>

Состояние и Last error дополняются счётчиками текущего запуска порта: запросы, ответы, тайм-ауты, восстановления, очередь, CRC и ошибки протокола, RAW-байты. Ноль — числовое значение счётчика; прочерк в таблице событий имеет другой смысл. Счётчики зависят от транспорта, не являются постоянным архивом измерений и не доказывают отсутствие потерь в непроверенных режимах.

### Система — Web Server, подсветка и время

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-system-web-server-http-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-system-web-server-http-en.png" alt="web-system-web-server-http-en" width="600"></a></td><td><pre>Web
└─ System
   └─ Web Server
      └─ Protocol: HTTP ←</pre></td></tr></table>

Web Server содержит State, Interface (LAN1/LAN2/Both) и Protocol (HTTP/HTTPS). Save применяет выбранные параметры; изменение протокола или интерфейса может разорвать текущий Web-сеанс. Откройте адрес с нужной схемой http:// или https://, показанный LCD F5 URLs. Backlight имеет отдельную кнопку Save. NTP сохраняет включение, сервер и интервал 1/6/24 часа; Manual time отдельно устанавливает дату и время. Test NTP выполняет до трёх попыток с минутным интервалом, Stop NTP test останавливает тест. Эти кнопки не заменяют сохранение настроек NTP.

### Система — Security и остановка

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-system-security-password-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-system-security-password-en.png" alt="web-system-security-password-en" width="600"></a></td><td><pre>Web
└─ System
   └─ Security
      └─ Change password ←</pre></td></tr></table>

Для смены пароля введите текущий пароль, новый и повтор нового. Save сохраняет пароль и возвращает к входу с новым паролем. Глаза расположены внутри всех трёх полей. Stop Gateway требует подтверждения, останавливает приложение и последовательные транспорты; Web также отключается. Это не выключение питания ОС. В HTTPS ниже показываются TLS-состояние, SHA-256 сертификата и срок действия; в HTTP этот блок отсутствует.

### Справка — Help

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-help-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-help-en.png" alt="web-help-en" width="600"></a></td><td><pre>Web
└─ Help ←</pre></td></tr></table>

Встроенная справка содержит начало работы, настройки Gateway, соединения и кабели, обслуживание, диагностику, сведения об оборудовании и источниках. Изображения — примеры, их IP и параметры не следует копировать как значения по умолчанию. Полное актуальное дерево LCD приведено в начале этого руководства.

### О программе — About

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-about-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-about-en.png" alt="web-about-en" width="600"></a></td><td><pre>Web
└─ About ←</pre></td></tr></table>

Показывает продукт, версию, модель, ядро и архитектуру. При обращении по ошибке сообщите эти данные и точное действие, после которого возникла проблема. Вверху панели доступны язык RU/EN, переключение темы и выход из сеанса.

