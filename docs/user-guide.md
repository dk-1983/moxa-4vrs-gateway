# 4VRS Gateway v2026.02.03 — User guide

[README](../README.md) · [LCD / Web](screens.md) · [Release scope](release-final.md)

## Contents

- [Vendor firmware requirement](#vendor-firmware-requirement)
- [Installation](#installation)
- [Menu tree](#menu-tree)
- [Web panel tree](#web-panel-tree)
- [Operation and navigation](#operation-and-navigation)
- [Home screen](#home-screen)
- [Serial ports and transports](#serial-ports-and-transports)
- [Network settings](#network-settings)
- [Clock and NTP](#clock-and-ntp)
- [Diagnostics and stopping](#diagnostics-and-stopping)
- [Quick troubleshooting](#quick-troubleshooting)
- [Web on UC-7420-LX Plus](#web-on-uc-7420-lx-plus)
- [LCD — backlight and Web Server](#lcd--backlight-and-web-server)
- [Web — pages and actions](#web--pages-and-actions)

## Vendor firmware requirement

Supported platforms are UC-7420-LX Plus with OS 1.6 / Linux 2.6.10 and UC-7420-LX without Plus with OS 2.3 / Linux 2.4.18. The universal installer selects the profile on the device; no OS choice is needed in the CF wizard. Other models are not qualified.

The vendor firmware requirements below apply to **Plus**.

Before installing 4VRS Gateway on **Moxa UC-7420-LX Plus**, upgrade the vendor
firmware to **1.6** (`FWR_UC7400P_V1.6_Build_09110414`). This is the required
baseline for application installation under this guide.

Run on the device:

```sh
kversion
```

Expected output:

```text
UC-7420-LX Plus firmware version 1.6
```

As checked on 2026-09-10, version 1.6 is the latest published ready-to-install
Linux 2.6.x firmware for this platform in the [Moxa catalogue](https://www.moxa.com/en/products/phased-out-products/uc-7410%2C-uc-7420-series).
Version 2.3 listed there belongs to the Linux 2.4.x branch; do not select an
image solely by its higher version number. The image must match **LX Plus**.

Upgrade vendor firmware **before installing Gateway**, separately from the CF
wizard and application installer. It erases internal Flash data and settings:
save required settings first and retain console access. Do not interrupt power
during flashing. After reboot, repeat `kversion` and check networking using the
[manufacturer manual](https://www.moxa.com/Moxa/media/PDIM/S100000489/moxa-uc-7410-lx-plus-series-software-manual-v6.0.pdf),
Upgrading the Firmware, pages 3-4–3-5.

## Installation

The full package and CF wizard are available in the [release](https://github.com/dk-1983/moxa-4vrs-gateway/releases/tag/v2026.02.03). Follow the [current commissioning procedure](release-final-commissioning.md), including explicit initial RNG activation. Earlier manual-install procedures do not apply to this release.

## Menu tree

Names below match the display. In ordinary lists, **F2/F4** move the selection,
**F3** opens an item and **F1** returns. Follow the bottom-line prompts for editor actions.

```text
Home screen
├── F1 Help
└── F3 Main Menu
    ├── Status — application state
    ├── Ports
    │   └── P1 … P8 → F3 Detail
    │       └── F2/F4: connection → counters → errors → port state
    ├── Configuration
    │   ├── P1 … P8 → F3 Select
    │   │   ├── Port settings → F5 Edit
    │   │   ├── Save & Apply → confirmation → result
    │   │   └── Cancel changes
    │   └── F5 Network
    │       ├── F2/F4: LAN2 / LAN1 / route / DNS / Observed LAN1 / Observed LAN2
    │       └── F5 Edit → F5 Review → F3 Apply
    │           ├── F3 Keep — save
    │           └── F1 Revert / timer expiry — roll back
    ├── Diagnostics
    │   └── F3 Events → Startup Events → F2/F4: entries
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
    └── About — product and version
```



System opens at Date & Time. F4 cycles through Platform → Display → Web Server → Date & Time; F2 moves backwards. Thus F2 from Date & Time reaches Web Server directly. F3 opens Display or Web Server; F5 opens Network Time only on Platform. Tree labels use the LCD language.

## Web panel tree

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

## Operation and navigation

## Home screen

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/home.png" alt="Home screen and physical keys" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre><b>▶ Home screen</b></pre>
</td>
</tr>
</table>

*Home screen.*

Use the device's existing network address. A migration must retain its own
configuration; do not copy another Moxa's saved network profile.

On Home, **F3** opens the main menu and **F1** opens Help. The menu contains
Status, Ports, Configuration, Diagnostics, System, Shutdown and About.
The bottom screen lines show the keys available on the current page.
Usually F2/F4 move between choices, F3 selects and F1 goes back.

`READY` and `Ports … ready` describe application/port readiness. They do not
prove that an instrument answered. Check a real client transaction as well.
`Clients 0` simply means there is no counted client at that moment.

### Main menu

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/main-menu.png" alt="Main menu with Status selected" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── <b>▶ F3 → Main Menu</b></pre>
</td>
</tr>
</table>

*Main menu.*

F2/F4 move the selection, F3 opens it and F1 returns to Home. Use Status for
application state, Ports for per-port counters, Configuration for settings,
Diagnostics for diagnostic information/startup events, System for the clock, NTP, display
and Web Server, and About for the version. Shutdown stops the application only.

## Serial ports and transports

Open **Configuration**, select a physical port, and review its fields. Match
serial mode, baud rate, data bits, parity and stop bits to the instrument.
Logical P1–P8 are tied to their physical UARTs. Confirm wiring using the device
documentation; Ethernet-style RJ45 connectors do not imply Ethernet signaling.

Select the field and use the displayed **F5 Edit** action. F2/F4 change a value;
follow the field's prompts to accept it. Complete the port's **Apply** and
confirmation steps to submit the configuration: the final field is
**Save & Apply**. **Cancel changes** abandons the draft.
A field edit alone is not a saved configuration.

Wait for **Config Result** after confirming Apply. `APPLYING...` is still in
progress. Plain `APPLIED` means the configuration transaction succeeded,
including persistence; `NO CHANGES` means there was nothing to change.
`INVALID / NOT APPLIED` rejects the draft. `FAILED / ROLLED BACK` reports a
failed change and rollback; `ROLLBACK FAILED` needs diagnosis. If `APPLIED`
is accompanied by `DURABILITY?`, persistence is uncertain: do not treat it as
a confirmed save across power loss. Record the result and diagnose storage.

| Panel transport | Client framing / purpose |
| --- | --- |
| MB TCP | Standard Modbus TCP with MBAP header |
| MB UDP | MBAP-framed Modbus in UDP |
| RTU/UDP | Complete Modbus RTU frames, including CRC, in UDP |
| RAW TCP | Transparent TCP byte stream to/from serial |
| RAW UDP | Transparent UDP-to-serial data exchange |

The Modbus modes bridge to serial RTU. The instrument address is the Modbus
unit address in the client's requests; the gateway's IP address is a different
setting. RAW transports do not turn arbitrary bytes into Modbus transactions.
Choose the same transport at both network endpoints.

The new-configuration defaults are RS-232, 9600, 8N1, Modbus TCP, all eight
ports enabled, network ports 502–509. Existing saved configurations can differ.
Select an appropriate listener address and an unused endpoint port. `0.0.0.0`
is a wildcard bind, not a destination address to enter in a client.

Under **Ports**, select a port to inspect its state and page through counters
and errors with F2/F4. An established TCP connection proves socket access;
a valid read response is needed to prove the complete instrument path.

### Inspecting ports

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/ports.png" alt="Ports list with P1 selected" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── F3 → Main Menu
   └── <b>▶ F3 → Ports</b></pre>
</td>
</tr>
</table>

*Port list.*

F2/F4 move through all eight ports, scrolling the five-row list as needed.
Press F3 Detail to inspect the selected port; F2/F4 then switch detail pages.
F1 returns to the list. This view reports status; it does not edit configuration.

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/port-details.png" alt="First port detail page" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── F3 → Main Menu
   └── F3 → Ports
      └── <b>▶ F3 → P1 → Detail</b></pre>
</td>
</tr>
</table>

*Port connection settings.*

In this example, `115200 8NONE1` means 115200 baud, 8 data bits, no parity,
1 stop bit. `TCP 1502` is the listener port, not the Modbus unit address.

The detail header shows the physical port and page number, for example
`Port 1: 1/4`. There are four pages:

| Page | Fields |
| --- | --- |
| 1/4 — Connection | UART (`ttyM0` is P1), serial mode, baud/framing, transport, bind address, TCP/UDP port and clients |
| 2/4 — Transactions | Accepted, Complete, Timeout, Recovery, current Queue and maximum observed High water |
| 3/4 — Protocol diagnostics | CRC and Frame errors, Unit/Function mismatches, leading Garbage and Stale responses |
| 4/4 — Runtime | Configuration Generation, Last ok and the runtime error text |

`Queue` is current depth; `High water` is the recorded peak. Counters describe
the selected runtime/transport and are not water-meter readings or persistent
instrument totals. Interpret Modbus-specific diagnostics in the context of
the selected transport. `Last ok` is a runtime timestamp, not a formatted
calendar time. A `!` beside a port in the list flags a recorded error; inspect
the details before deciding what caused it.

## Network settings

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/configuration.png" alt="Configuration entry point" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── F3 → Main Menu
   └── <b>▶ F3 → Configuration</b></pre>
</td>
</tr>
</table>

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/network-lan2.png" alt="Confirmed LAN2 settings" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── F3 → Main Menu
   └── F3 → Configuration
      └── F5 → Network
         └── <b>▶ LAN2</b></pre>
</td>
</tr>
</table>

*Configuration menu and LAN2 network settings.*

From the main menu open **Configuration**, then **F5 Network** on the port list.
F2/F4 cycle through LAN2, LAN1, Default route, Global DNS, Observed LAN1 and
Observed LAN2. Use F5 Edit on a settings page.

**Confirmed policy** is what was accepted and saved. **Observed** shows the
live interface state. They answer different questions, especially under DHCP.
`Link down` on an Observed page means the interface is currently reported
without link; it is not a statement that the saved IP address was erased.

For a LAN, edit the IP octets, subnet-mask octets and static/DHCP mode. F3 moves
to the next field; F2/F4 change it. Set addresses appropriate to your own
network. DHCP is a client function: the gateway does not distribute addresses.
A router-side MAC reservation is configured on the DHCP server separately.

Default-route settings select no default route, LAN1 or LAN2 and the gateway
address. Global DNS selects manual servers or automatic DNS from one chosen
DHCP interface. DNS source and default-route interface are separate settings.
An expired DHCP lease is not retained as a valid static address.

### Apply, Keep and Revert

1. Finish editing and press **F5 Review**.
2. Check the proposed values. Press **F3 Apply** to begin the temporary change.
3. During the confirmation countdown, test access using the new address.
4. Press **F3 Keep** to accept and save it. The result should be `KEPT`.
5. To undo it instead, press **F1 Revert**, or allow the timer to expire without
   pressing Keep. The successful rollback result is `REVERTED`.

The confirmation window is 60 seconds. **Keep saves; Revert undoes.** Read the
current screen prompts before pressing a key. Before Apply, F1 Cancel abandons
the editor; this is a different phase from reverting an applied change.

Keep confirms the network transaction; there is no second network Save step.
After a successful Keep, the confirmed policy is intended to survive restart.
If Apply or rollback reports an error, retain the message and use available
management access to inspect it rather than repeatedly applying another change.
For recovery to DHCP policy, lack of a DHCP server can legitimately leave the
interface waiting for a new lease.

## Clock and NTP

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/network-time.png" alt="Network Time settings and synchronization status" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── F3 → Main Menu
   └── F3 → System
      └── F4 → Platform
         └── <b>▶ F5 → Network Time</b></pre>
</td>
</tr>
</table>

Open **System**, go to its Platform page and open **Network Time** using the
on-screen prompts. Edit the NTP server, enable flag and interval, then confirm.
Supported normal intervals are **1, 6 and 24 hours**, with **1 hour** the default.
The server must be reachable; a hostname also requires working DNS.

With NTP enabled, **F4 Test** starts diagnostic operation at one-minute intervals.
It ends automatically after **three attempts**, then returns to the saved normal
interval. Three attempts does not necessarily mean three successful replies.
Use the displayed Stop action to end the test early.

Home can show `TIME UNSYNC`, `TIME SYNCING` or `TIME MANUAL`. In the synchronized
state the warning line disappears; the System page provides the explicit state.
A trustworthy RTC and a successful NTP synchronization are separate facts.
`TIME UNSYNC` alone does not diagnose a failed battery. Check NTP enable/server,
link, route, DNS and the System diagnostics before drawing that conclusion.

The clock service uses RTC in UTC. Keep network-time configuration saved and
verify automatic synchronization after installation or a planned restart.

## Diagnostics and stopping

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/diagnostics.png" alt="Diagnostics counters" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── F3 → Main Menu
   └── <b>▶ F3 → Diagnostics</b></pre>
</td>
</tr>
</table>

*Diagnostics counters.*

Open **Main Menu → Diagnostics** to view accepted and completed requests,
timeouts, recoveries, stale responses and the queue high-water mark.
**F3 Events** opens startup events.

<table>
<tr>
<td valign="top">
<img src="../assets/images/menu/startup-events.png" alt="Startup events" width="360">
</td>
<td valign="top">
<strong>Menu path</strong>
<pre>Home screen
└── F3 → Main Menu
   └── F3 → Diagnostics
      └── <b>▶ F3 → Startup Events</b></pre>
</td>
</tr>
</table>

*Startup events.*

Use **F2/F4** to browse events. Each entry shows its sequence, startup stage,
progress, result and error. `BOOT / Progress 0%` describes the selected startup
event, not the application's current readiness.

Use **Diagnostics** for startup events and **About** for the product version.
For an issue report record the model, version, affected port, transport, serial
settings, operation performed and exact displayed result. Remove credentials
and private infrastructure details from shared logs.

**Shutdown stops the Gateway application. It does not power off or reboot the
Moxa operating system.** Coordinate OS restart or physical power removal
separately. Do not start a second Gateway instance against the same UARTs.

Confirmation shows stopping progress and the final screen. `Gateway stopped` means the application has exited; the OS is still running. `Shutdown failed` shows the reason; F1 returns to the menu without restarting stopped ports. [Screen and details](releases/v2026.02.03-lcd.md).

## Quick troubleshooting

| Symptom | First checks |
| --- | --- |
| No connection after moving an instrument | Client's destination Moxa IP/port and actual serial-port wiring |
| Connection succeeds, no instrument data | Unit address, serial mode/baud/parity, transport framing and instrument state |
| Network change returns to old values | Was Keep pressed before expiry? What result/error was shown? |
| Confirmed IP exists, live link is down | Cable, switch port and the matching physical LAN |
| DHCP interface waits for network | Link and DHCP server; do not assume an expired lease remains valid |
| NTP remains unsynchronized | Enable flag, server, route, DNS and the clock diagnostic result |

The Web instructions below apply to release `v2026.02.01`.
Automatic updating remains a separate future feature.

## Web on UC-7420-LX Plus

HTTPS is not recommended for this device due to additional processing overhead and connection delays. Password verification during sign-in causes a temporary high CPU load — approximately **92% in testing** — regardless of the protocol.

After optimization, normal Web panel use places little load on the processor: in the user-operated HTTP test, the Web process averaged approximately **0.06% CPU**, excluding the separate password-verification process. After sign-in, the interface is responsive in both modes.

HTTP is recommended for a dedicated trusted management network and remains the default for new installations. HTTP sends passwords and data without encryption. HTTPS (TLS) remains available, but is not recommended on Moxa UC-7420-LX Plus. Upgrades preserve explicit HTTP/HTTPS choices; schema 1/2 configurations without a protocol field retain legacy HTTPS. A single-client limit and password hashing do not protect data transmitted over HTTP.

[Measurement conditions / условия измерений](release-final.md).

## LCD — backlight and Web Server

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

On Display press F3, then F3 on Backlight; F2/F4 select On/Off and F3 saves. Stored is the saved choice; Command reports the backlight command result.

In Web Server, F2/F4 select one of five rows. Enable/Disable names the action: Disable with Running means the server is enabled. F3 changes the selected enable state, interface or protocol and starts persistence. Wait for Saving… to finish; repeated switching does not require leaving via F5/F1. On Save failed or Config conflict, check the outcome and reopen the page. New code issues a one-time code; Recover access requires confirmation. F5 opens addresses; Unavailable means no usable URL on that interface. In HTTPS, F5 Cert shows the fingerprint: compare all 64 SHA-256 digits against the browser certificate. F1 returns to Web Server.

## Web — pages and actions

Only one Web TCP connection is served at a time; additional TCP connections are closed before TLS without replacing the active one. This is a connection limit, not a guarantee of one tab or one user. If a connection is refused, close unnecessary connections and retry after the active one is released.

### Sign-in and access recovery

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-sign-in-security-help-ru.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-sign-in-security-help-ru.png" alt="web-sign-in-security-help-ru" width="600"></a></td><td><pre>Web
└─ Вход администратора
   └─ Справка о соединении ←</pre></td></tr></table>

Create the first administrator using the one-time LCD code and your own password. The code lasts three minutes with at most five attempts. Request another through LCD System → Web Server → New code. Later sign-ins require the password only. Password length is 12–128 UTF-8 bytes, not necessarily 12–128 characters. The eye inside each field shows or hides that field. Forgot password? requests physical confirmation: press F3 on the device, then enter the display code and a new password. This recovers Web access; it does not reset port configuration.

### Overview

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-overview-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-overview-en.png" alt="web-overview-en" width="600"></a></td><td><pre>Web
└─ Overview ←</pre></td></tr></table>

Shows port readiness, TCP connections, port alarms, Web/clock state and OS uptime. Refresh updates the snapshot; Diagnostics opens diagnostics. Check the update timestamp: a stale snapshot does not establish current state. Port readiness and a TCP connection alone do not prove a successful instrument response.

### Ports

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-ports-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-ports-en.png" alt="web-ports-en" width="600"></a></td><td><pre>Web
└─ Ports ←</pre></td></tr></table>

The P1–P8 list shows enable state, UART, transport and listener address. Edit opens the selected physical port. Disabled does not mean the hardware port is absent. 0.0.0.0 is a wildcard listener; clients connect to the actual device IP.

### Port settings

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-port-p1-settings-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-port-p1-settings-en.png" alt="web-port-p1-settings-en" width="600"></a></td><td><pre>Web
└─ Ports
   └─ P1 → Edit ←</pre></td></tr></table>

Set State, Serial mode, Baud, Data bits, Parity, Stop bits, Transport, Bind IP, TCP/UDP port and Special baud when needed. Save submits changes. Check the result and actual communication: changing UART or transport can interrupt existing connections. If LCD changes caused a configuration conflict, reopen the form and review its values.

### Network

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-network-settings-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-network-settings-en.png" alt="web-network-settings-en" width="600"></a></td><td><pre>Web
└─ Network ←</pre></td></tr></table>

LAN1 and LAN2 have separate Static/DHCP, IP, netmask and gateway settings. Default route selects the route interface; DNS uses manual servers or a DHCP source. Observed shows live state, not just saved policy. Review displays the complete draft without applying it. Apply starts temporary activation; reconnect to the new address if needed. Keep confirms persistence within 60 seconds. Revert or timeout restores the previous network. Do not submit another network draft while confirmation is pending.

### Diagnostics — startup events

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-startup-events-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-startup-events-en.png" alt="web-diagnostics-startup-events-en" width="600"></a></td><td><pre>Web
└─ Diagnostics
   └─ Startup Events ←</pre></td></tr></table>

The table is a startup event history, not the current boot percentage. A dash in Ports means the event has no associated port; a dash in Error means no error is reported for it. These are not missing counters. Starting rows remain historical events after Ready / Completed / 100%. P1–P8 buttons open port details.

### Port diagnostics

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-port-p1-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-diagnostics-port-p1-en.png" alt="web-diagnostics-port-p1-en" width="600"></a></td><td><pre>Web
└─ Diagnostics
   └─ P1 ←</pre></td></tr></table>

State and Last error accompany counters for the current port run: requests, completions, timeouts, recoveries, queues, CRC/protocol errors and RAW bytes. Zero is a counter value; event-table dashes have a different meaning. Counters depend on the transport, are not a persistent measurement archive, and do not prove losslessness in unqualified scenarios.

### System — Web Server, backlight and time

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-system-web-server-http-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-system-web-server-http-en.png" alt="web-system-web-server-http-en" width="600"></a></td><td><pre>Web
└─ System
   └─ Web Server
      └─ Protocol: HTTP ←</pre></td></tr></table>

Web Server contains State, Interface (LAN1/LAN2/Both) and Protocol (HTTP/HTTPS). Save applies the settings; changing protocol or interface may disconnect the Web session. Open the matching http:// or https:// address shown by LCD F5 URLs. Backlight has its own Save button. NTP saves enable, server and 1/6/24-hour interval; Manual time sets date/time separately. Test NTP performs up to three attempts at one-minute intervals; Stop NTP test ends it. These controls do not replace saving the NTP configuration.

### System — Security and stopping

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-system-security-password-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-system-security-password-en.png" alt="web-system-security-password-en" width="600"></a></td><td><pre>Web
└─ System
   └─ Security
      └─ Change password ←</pre></td></tr></table>

To change the password, enter the current password, new password and its repeat. Save stores it and returns to sign-in with the new password. All three fields have an eye inside. Stop Gateway requires confirmation and stops the application and serial transports; Web disconnects too. It does not power off the OS. In HTTPS, a following section displays TLS state, certificate SHA-256 and expiry; HTTP omits that section.

### Help

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-help-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-help-en.png" alt="web-help-en" width="600"></a></td><td><pre>Web
└─ Help ←</pre></td></tr></table>

Built-in Help covers getting started, Gateway settings, connections/cables, maintenance, diagnostics, hardware and sources. Images are examples: do not copy their IPs or settings as defaults. The complete current LCD tree is at the start of this guide.

### About

<table><tr><td><a href="../assets/images/screenshots/web-v2026.02.01-public/web-about-en.png"><img src="../assets/images/screenshots/web-v2026.02.01-public/web-about-en.png" alt="web-about-en" width="600"></a></td><td><pre>Web
└─ About ←</pre></td></tr></table>

Shows product, version, model, kernel and architecture. Include these details and the exact triggering action in an issue report. The panel header provides RU/EN language, theme switching and sign-out.

