# 4VRS Gateway user guide

[README](../README.md) · [Русский](user-guide.ru.md)

Draft for the first public release, v2026.00.01. Describes the current physical
panel implementation. Public installation packaging and remaining hardware
qualification are still in progress. This guide describes the 4VRS Gateway project. For the device's technical,
electrical and other specifications, consult the manufacturer's instructions.

## Home screen

<img src="../assets/images/menu/home.png" alt="Home screen and physical keys" width="360">

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

<img src="../assets/images/menu/main-menu.png" alt="Main menu with Status selected" width="360">

*Main menu.*

F2/F4 move the selection, F3 opens it and F1 returns to Home. Use Status for
application state, Ports for per-port counters, Configuration for settings,
Diagnostics for diagnostic information/startup events, System for the clock
and platform, and About for the version. Shutdown stops the application only.

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

<img src="../assets/images/menu/ports.png" alt="Ports list with P1 selected" width="360">

*Port list.*

F2/F4 move through all eight ports, scrolling the five-row list as needed.
Press F3 Detail to inspect the selected port; F2/F4 then switch detail pages.
F1 returns to the list. This view reports status; it does not edit configuration.

<img src="../assets/images/menu/port-details.png" alt="First port detail page" width="360">

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

<img src="../assets/images/menu/configuration.png" alt="Configuration entry point" width="360">

<img src="../assets/images/menu/network-lan2.png" alt="Confirmed LAN2 settings" width="360">

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

<img src="../assets/images/menu/network-time.png" alt="Network Time settings and synchronization status" width="360">

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

<img src="../assets/images/menu/diagnostics.png" alt="Diagnostics counters" width="360">

*Diagnostics counters.*

Open **Main Menu → Diagnostics** to view accepted and completed requests,
timeouts, recoveries, stale responses and the queue high-water mark.
**F3 Events** opens startup events.

<img src="../assets/images/menu/startup-events.png" alt="Startup events" width="360">

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

## Quick troubleshooting

| Symptom | First checks |
| --- | --- |
| No connection after moving an instrument | Client's destination Moxa IP/port and actual serial-port wiring |
| Connection succeeds, no instrument data | Unit address, serial mode/baud/parity, transport framing and instrument state |
| Network change returns to old values | Was Keep pressed before expiry? What result/error was shown? |
| Confirmed IP exists, live link is down | Cable, switch port and the matching physical LAN |
| DHCP interface waits for network | Link and DHCP server; do not assume an expired lease remains valid |
| NTP remains unsynchronized | Enable flag, server, route, DNS and the clock diagnostic result |

Web UI and automatic updating are planned for a later feature release and are
not described as available controls in this version.
