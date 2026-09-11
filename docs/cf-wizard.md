> [Current commissioning / Актуальный ввод в эксплуатацию](release-final-commissioning.md).

# CompactFlash installation and update — Ubuntu 24.04

> Current [release matrix](release-final.md). The user reduced the series to 10; 10/10 completed. Hardware no-op acceptance is closed; ready for release within the matrix scope. No power-series repeat is required.

> Current candidate — **web-ipc-v1**: [safe Web IPC recovery after power loss](web-ipc-lifecycle.md). The agreed 10/10 power-cycle series is complete; the previous FAIL belongs to the old candidate.

> Current local candidate includes the [slow NV commit fix](rng-autonomous-v1-slow-commit.md):
> up to 30 seconds for RNG, stop after commit outcome, installer compatibility with schema 3.

Current local bundle uses autonomous RNG schema 3. The wizard verifies schemas 1/2/3, including matching state/witness; new CF requires one explicit activation after preparation. See [policy and commands](rng-autonomous-v1.md). Normal restarts require no service.

[Русский](cf-wizard.ru.md)

Offline tool for Ubuntu 24.04 **x86-64** and Moxa UC-7420-LX Plus. The kit includes
the complete qualified Web candidate and the existing native CF-bootstrap worker.
It never connects to a Moxa or changes factory firmware. PC preparation ends with
**activation pending**, not with a commissioned device.

## Vendor firmware requirement

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

## Run on Ubuntu

Install `python3 util-linux fdisk e2fsprogs udev unzip`. Compare the ZIP SHA256
with the delivery report. Extract on the PC, outside CF, into a new root-owned
directory, then verify the kit:

```sh
sudo install -d -m 0755 /opt/4vrs-cf-install-update
sudo unzip 4vrs-cf-wizard-v2026.02.01-final-review.zip -d /opt/4vrs-cf-install-update
cd /opt/4vrs-cf-install-update
sha256sum -c SHA256SUMS
sudo python3 cf-wizard.py --list
sudo python3 cf-wizard.py --report /root/cf-preparation-result.json
```

Use a new report filename. Disable removable-media automount, close applications
using the card, and unmount it in Ubuntu Disks before starting. Keep the same card
inserted throughout; do not run another storage tool concurrently. The wizard
never unmounts another application's mount or clears read-only flags.

Select the displayed card ID after checking USB VID:PID, reader serial, exact byte
size, partitions and UUID. Current `/dev/sdX` names are informational, never saved
as permanent identities. Reader serial identifies the reader, not an individual CF.
Mounted/system disks, swap, holders, mapped storage, non-USB disks and ambiguous
readers are blocked. If the system root disk cannot be resolved, writes are refused.
Partition parentage and media identity are checked again immediately before writes.

Enter the target name, IPv4:port and LAN1 MAC from a trusted inventory of the
selected unit; repeat the MAC. There is no fixed Moxa 1 target. RNG binds to that
MAC and the new/existing filesystem UUID. No network discovery occurs.

## 1 — First installation: erase everything

Type the exact **ERASE-ALL-DATA** phrase containing USB ID, serial, byte size and
card ID. This deletes all data, including old RNG, configurations and test folders.
Old ext3, dirty journal / `needs_recovery`, other signatures and old RNG do not
block this explicitly confirmed path. The old filesystem is never mounted or
repaired and its journal is never replayed. Existing contents are not backed up.
The scan shows partition metadata; it does not inspect dirty filesystem contents.

The wizard creates a DOS/MBR table with one Linux partition starting at sector
2048 and ext3 with 4096-byte blocks, 128-byte inodes and explicitly bounded legacy
features. It checks the new filesystem, mounts it, creates fresh RNG using the
existing worker and PC `getrandom`, and writes all 11 Gateway package files.
It unmounts, verifies RNG and package after read-only mounting, then unmounts normally.
RNG is never silently regenerated on a retry: a new erase requires a new explicit
confirmation. An interrupted first installation is incomplete.

## 2 — Update an existing installation

Requires one compatible, clean ext3/MBR partition, existing Gateway binary and
configuration schema 1, 2 or 3, and valid RNG bound to the selected MAC/UUID.
`e2fsck -f -n` runs without repair before mounting. Dirty/`needs_recovery` stops
the update before mount; do not bypass it or repair automatically. Missing RNG,
pending RNG, wrong binding, unsafe ownership, symlinks or unsupported schema stop
the update. The wizard never provisions RNG in this scenario.

The first mount is `ro,noload,nodev,nosuid,noexec`; used bytes and escaped top-level
names are displayed without traversing private state. Confirm the exact **UPDATE**
phrase. The complete package is staged separately under
`4vrs-packages/gateway-f519719cadca03b9`. Active `4vrs/bin`, configurations,
administrator/password state, TLS certificate/private key and current RNG are
unchanged. Existing files are never copied into the portable kit or report.

Writes use a root-owned `.pending` directory, exclusive new files, file/directory
fsync and a final rename only after byte/mode verification. An identical ready
package is verified as a no-op; unexpected or interrupted contents cause refusal.
Do not delete pending work or retry blindly. Active software remains usable until
the separate target installer is run. Cancelling PC staging requires no program rollback.

## Separate first start / activation on the selected Moxa

After **Unmounted**, remove CF safely. In a separately authorized maintenance
window, shut down the selected unit normally before inserting/removing CF.
Confirm its address, LAN1 MAC, CF UUID and `/var/hda` mount; do not substitute a unit.
Keep the previous full package and protected recovery baseline. On that Moxa:

Run these commands from the package directory reported by the wizard:

```sh
./4vrs-install
./4vrs-install --status
```

The unchanged native installer performs full configuration decoding, platform,
package ABI and startup-layout checks, and transactional activation with recovery
journals. It may disconnect SSH; reconnect and inspect
`/etc/4vrs-installer/recovery --status`. An accepted launch is not success.
After examining a failed/interrupted transaction, the existing recovery entry is
`/etc/4vrs-installer/recovery --recover`. Do not delete its journals. Automatic
failure rollback is not a universal downgrade command. Returning to old candidates
that cannot decode schema 3 requires a compatible public configuration; never
restore obsolete RNG, password or TLS state with a software rollback.

The PC compatibility check is structural; complete configuration and target startup
checks remain the installer's responsibility. Fresh RNG reports `policy=required`:
the wizard creates no `trial-policy` and does not authorize its use. Explicit
`production-enable` with fresh external input is required; see the [commissioning procedure](release-final-commissioning.md). No secrets should
be pasted into reports or logs. Do not export a card image containing private state.

Verify installer result, Gateway, expected UART/Modbus traffic, network, LCD and
Web before declaring commissioning complete. New installations default to HTTP;
updates preserve explicit HTTP/HTTPS selection and legacy HTTPS migration. HTTP
sends passwords and data without encryption: use a dedicated trusted management
network. HTTPS remains selectable but is not recommended on this device because
of processing overhead and connection delays.

Qualification uses file images with an injected mount boundary and public test
RNG, not physical CF. Real mounting, removal, power loss and hardware commissioning
remain unverified by this local task. On any failure, preserve files, check mounts,
and do not remove the card until it is normally unmounted.

[Exact commissioning procedure](release-final-commissioning.md).
