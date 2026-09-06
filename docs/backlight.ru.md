# Подсветка дисплея

[English](backlight.md) · [Руководство](user-guide.ru.md)

**System → Display → Backlight → On / Off**. F4 переводит страницы System до
Display; F3 открывает Display, затем Backlight. F2/F4 выбирают; **F3 сохраняет**.
F1 возвращает без сохранения выделенного значения. Default On; старые config
без поля также получают On. Сохранённый Off применяется при запуске.

Выключается только свет: содержимое экрана и кнопки остаются активны. Вернитесь
тем же путём и включите On. Ошибка аппаратной команды не показывается как успех;
статус означает подтверждение команды, а не выдуманный readback. Gateway/UART/
сеть не перезапускаются. Старые декодеры не понимают Off-поле — перед downgrade
проверить основной config и backup. [Границы доказательств](validation.ru.md).

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/system-display.jpg" alt="System: Display" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── F3 → System
      └── <b>▶ F4 × 2 → Display</b></pre>
</td>
</tr>
</table>

*System: Display.*

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/display-menu.jpg" alt="Меню Display" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── F3 → System
      └── F4 × 2 → Display
         └── <b>▶ F3 → Display</b></pre>
</td>
</tr>
</table>

*Меню Display.*

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/backlight-on.jpg" alt="Настройки подсветки" width="360">
</td>
<td valign="top">
<strong>Путь в меню</strong>
<pre>Главный экран
└── F3 → Main Menu
   └── F3 → System
      └── F4 × 2 → Display
         └── F3 → Display
            └── <b>▶ F3 → Backlight</b></pre>
</td>
</tr>
</table>

*Настройки подсветки.*
