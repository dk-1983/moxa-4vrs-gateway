> v2026.02.01: follow the [current commissioning procedure](release-final-commissioning.md), including separate RNG activation. See [release limits](release-final.md).

# Automatic installation — v2026.01.01

[Русский](installation.ru.md) · [User guide](user-guide.md) · [Validation](validation.md)

The native installer runs on supported Moxa UC-7420-LX Plus Linux systems.
It preserves the device's own network, serial and NTP configuration, prepares
the network/clock scripts and manages services/startup. It needs no Python on
the Moxa, no manual enroll and no hand-written vendor derivative. It does not
reboot automatically and is not a vendor firmware image.

Arrange a maintenance window and recovery access; installation can interrupt
SSH and serial traffic. Preserve an external baseline of this device's settings
and startup files. Check target identity and actual mounted CF before proceeding.
Unsupported layouts are refused; do not bypass the refusal. Read the
[documented scope and deferred tests](validation.md) before first deployment.
Run each command separately and stop if it fails. Do not disable SSH host-key checks.

## Download and copy

**Computer — PowerShell**, in a download directory:
```powershell
Invoke-WebRequest -Uri 'https://github.com/dk-1983/moxa-4vrs-gateway/releases/download/v2026.01.01/4vrs-gateway-v2026.01.01.tar.gz' -OutFile './4vrs-gateway-v2026.01.01.tar.gz'
Invoke-WebRequest -Uri 'https://github.com/dk-1983/moxa-4vrs-gateway/releases/download/v2026.01.01/SHA256SUMS' -OutFile './SHA256SUMS'
Get-FileHash -Algorithm SHA256 './4vrs-gateway-v2026.01.01.tar.gz'
$moxaAddress = Read-Host 'Moxa IP'
$moxaSshPort = [int](Read-Host 'SSH port')
ssh -p $moxaSshPort "root@$moxaAddress"
```

Require package SHA256 `2009f886c8efae678548f609642ed8e5158f1c29add3fcc4a94f19a18d736faf`
and size 225757 bytes. SHA256SUMS covers the public archive, not the older release's ELF.

**Moxa — SSH:** require root, mounted CF at `/var/hda`, free space and the correct
device. The parent `/var/hda/4vrs/tests` must exist; for a first deployment create
it with `mkdir -p /var/hda/4vrs/tests` only after verifying CF is mounted.
Create a fresh staging directory; if it already exists, inspect it instead of overwriting:
```sh
id
mount
df -k /etc /var/hda
test ! -e /var/hda/4vrs/tests/install-v2026.01.01
mkdir -m 700 /var/hda/4vrs/tests/install-v2026.01.01
```

**Computer — PowerShell**, in the same shell holding the address/port variables:
```powershell
scp -P $moxaSshPort ./4vrs-gateway-v2026.01.01.tar.gz ./SHA256SUMS "root@${moxaAddress}:/var/hda/4vrs/tests/install-v2026.01.01/"
```

## Install and read the result

**Moxa — SSH:** require archive MD5 `cabf4fbce1b8b8e8b91b0e19e810959f`
after transfer before unpacking. Tar preserves executable modes; do not unpack
on Windows and upload mode-less files. Execute only the complete original package:
```sh
cd /var/hda/4vrs/tests/install-v2026.01.01
md5sum 4vrs-gateway-v2026.01.01.tar.gz
tar -xzf 4vrs-gateway-v2026.01.01.tar.gz
cd 4vrs-gateway-v2026.01.01
./4vrs-install
```

`operation started` means a detached worker was launched, not successful completion.
After reconnecting to the same device, read the persistent result:
```sh
cd /var/hda/4vrs/tests/install-v2026.01.01/4vrs-gateway-v2026.01.01
./4vrs-install --status
```

| result | Meaning |
| --- | --- |
| 0 | Completed operation; check stage (normally verify-installed; recovery repair may report repair-recovery-executable) |
| 3 | Healthy already installed/no transaction; no unnecessary service restart |
| 100 | Last worker record was running; may be stale after interruption/boot |
| 1 | Previous installation restored; requested update did not complete |
| 2 | Waiting for CF; do not bypass startup protection |
| -1 | Refused preparation/input; inspect stage/detail |
| -2 | Recovery required or incomplete; not a successful install |

`--status` exit0 means the record was read. Inspect result/stage, retained settings
and application readiness; TCP connection alone is not an instrument read.
To repeat a healthy installation run `./4vrs-install` from that package directory.
The installer preserves configuration; it does not copy another device's IPs.

## Interruption and recovery

Retain journals, gates, locks and network store. Do not erase them to force a retry.
The independent recovery executable works before CF becomes available:
```sh
/etc/4vrs-installer/recovery --recover
/etc/4vrs-installer/recovery --status
```

Read status after the asynchronous operation. If recovery has not yet been
published, rerun the same verified package; bootstrap had not changed the old
startup entries at that point. For WAIT_CF restore availability using an agreed
safe procedure, then retry recovery; do not manufacture a mount or hot-remove CF.
Boot entries also recover unfinished transactions. The old worker result100 can
persist after successful boot recovery; compare actual restored set/services.
Application startup waits up to120 seconds for CF; later recovery/start may need
an explicit retry. Corrupt journals require diagnosis via reserve access, not bypass.
Recovery rolls back an unfinished transaction; it is not a general downgrade command.

## Backlight and retained manual procedure

**System → Display → Backlight → On / Off**, F3 saves. Default On; old configs
without the field use On. Off retains screen/key handling and returns after boot.
See [Backlight](backlight.md). Older decoders cannot read an Off configuration or
backup: a binary downgrade needs separate compatibility review.

The [retained manual procedure](user-guide.md#manual-installation-of-v20260001)
describes the previous manually integrated release. Do not run it over this
installer's active journal or substitute old binaries into this package.
Its command checklist and operating instructions remain below the automatic path.
