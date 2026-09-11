# Initial commissioning: CF → install → explicit activation → Web

[Русский](release-final-commissioning.ru.md) · [Release matrix](release-final.md)

Commission during a maintenance window. Before writing, verify the selected device model/MAC and its CF UUID. Addresses and UUIDs in illustrations are examples, not target values. The wizard prepares CF; device installation and initial activation are separate steps.

## Prerequisites

You need a Moxa UC-7420-LX Plus with vendor firmware 1.6, CF and a USB reader, Ubuntu 24.04 x86-64 with root/sudo access, and console access to the Moxa. Ubuntu 24.04 in WSL2 is an option on Windows only after the USB reader is attached to WSL and visible in `lsblk`; a Windows drive letter is not a Linux device. The wizard refuses if it cannot safely identify the system disk or target card. Use native Ubuntu 24.04 in that case. Do not fully erase CF containing the only copy of required data.

Run commands individually and stop on any error. Use new directories. The wizard already contains the complete `gateway.tar.gz`; no separate installer download is required for this path.

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

## Procedure

1. On Ubuntu 24.04 x86-64, verify the distributed ZIP SHA256. Extract using unzip into a new root-owned PC directory outside CF:

   ```sh
   sudo install -d -m 0755 /opt/4vrs-v2026.02.01-final
   sudo unzip 4vrs-cf-wizard-v2026.02.01-ubuntu24-docs-r2.zip -d /opt/4vrs-v2026.02.01-final
   cd /opt/4vrs-v2026.02.01-final
   sha256sum -c SHA256SUMS
   sudo python3 cf-wizard.py --list
   sudo python3 cf-wizard.py --report /root/cf-v2026.02.01-new.json
   ```

   Confirm physical card, target MAC and destructive formatting only for the authorized new/spare CF. Verify the new ext3 UUID directly on PC against the report. Preserve the working card separately; never copy its RNG. Historical Bash UUID output is not authoritative. Worker file verification ends commissioned=false, activation pending.

The wizard unmounts CF after preparation. The fresh-input step is only for a new card, not an update of active schema 3. Before creating the input, identify that card’s ext3 partition again and compare its UUID with the wizard report. Do not mount over an occupied `/mnt/4vrs-cf`.

```sh
lsblk -o NAME,SIZE,FSTYPE,UUID,MOUNTPOINTS,MODEL,SERIAL
read -r -p 'CF partition path from lsblk: ' cf_partition
sudo blkid "$cf_partition"
sudo install -d -m 0700 /mnt/4vrs-cf
sudo mount -o nosuid,nodev "$cf_partition" /mnt/4vrs-cf
findmnt /mnt/4vrs-cf
```


2. New schema1 requires a separate **fresh 32-byte input** from the trusted Ubuntu PC CSPRNG, excluded from reports/ZIPs. After wizard completion, mount the same verified CF at root-owned /mnt/4vrs-cf with nosuid,nodev and verify device/UUID using findmnt/blkid. Each new filesystem has its own UUID. Create the one-time input:

   ```sh
   sudo python3 - <<'PY'
   import os
   from pathlib import Path
   root=Path('/mnt/4vrs-cf')  # manually confirm mount and UUID first
   seed=os.getrandom(32, os.GRND_NONBLOCK)
   if len(seed)!=32: raise RuntimeError('short entropy; stop')
   service=root/'4vrs-service'
   os.mkdir(service, 0o700)
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

   On error stop; never blindly reuse an existing directory/input or a partial file. Unmount cleanly and physically deliver CF to the powered-off unit. Do not send entropy through old SSH, arguments, clipboard, history or logs. Later unlink is not guaranteed NAND erasure; protect the medium.

After successful file creation:

```sh
sync
sudo umount /mnt/4vrs-cf
findmnt /mnt/4vrs-cf
```

The final command should show no mount (exit 1 for an absent mount). Remove the card only after successful umount. Insert it into the powered-off device.

3. On the device select `4vrs-packages/gateway-<first 16 SHA256 characters of gateway.tar.gz>` from the new SHA256SUMS. Verify MAC/UUID/configuration. Use the command sequence below once and poll status until terminal result. **Result100 is still running**; result0 verify-installed means installation completed, result3 already-installed means no-op. Investigate other results; a three-second timeout does not authorize relaunch. Before policy activation Web may remain closed.

Log into the Moxa as root through console or configured SSH. Do not copy another unit’s credentials. Confirm using `mount` that `/var/hda` is mounted CF, not an empty internal-memory directory; eth0 MAC must match the wizard target. The published installer uses this package path:

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

Repeat only `./4vrs-install --status` while `result=100`; do not relaunch installation. After a disconnection use `/etc/4vrs-installer/recovery --status`. `result=0` or `result=3` is a successful terminal result; investigate other results.

4. After terminal result, coordinate `/etc/init.d/4vrs-gateway stop` and confirm the RNG owner exited. For new consistent schema1 run **once**:


```sh
/etc/init.d/4vrs-gateway stop
ps
```

Wait until `4vrs-gateway` and `4vrs-rng` processes have exited; do not continue with a live RNG owner. Run production-enable below only for new schema 1.

   ```sh
   /var/hda/4vrs/bin/4vrs-rng production-enable /var/hda/4vrs-rng --fresh-entropy-confirmed < /var/hda/4vrs-service/fresh-rng-input
   ```

   On failure stop; service-recover is not automatic fallback. On success verify `4vrs-rng status /var/hda/4vrs-rng` reports ready/schema3, remove only the consumed fresh-rng-input and complete filesystem persistence. Never remove RNG state/witness/markers or reuse the input. Healthy active schema3 requires no activation.


```sh
/var/hda/4vrs/bin/4vrs-rng status /var/hda/4vrs-rng
```

Only after successful activation and status confirming ready schema 3:

```sh
rm /var/hda/4vrs-service/fresh-rng-input
sync
```

5. Run one normal `/etc/init.d/4vrs-gateway start`; verify Gateway/RNG/Web, generation, configured Web and required TCP ports. Check HTTPS/authentication separately when needed. Confirm gateway.conf preservation without exporting its contents. RNG ready is not system readiness; TCP connectivity is not proof of lossless Modbus. Subsequent ordinary restarts on consistent CF are autonomous.

The operator confirmed separate spare-card preparation, initial activation, installation and checks. This does not establish automatic end-to-end commissioning by the wizard. The procedure above separates installation and explicit policy transition from normal runtime. Do not repeat activation on the already commissioned card.

## Post-installation checks

```sh
/etc/init.d/4vrs-gateway start
ps
/var/hda/4vrs/bin/4vrs-rng status /var/hda/4vrs-rng
```

Start Gateway once. On LCD check required port readiness and System → Web Server → Running. F5 URLs shows the browser address. New installs use HTTP. For the first administrator select New code and set your own browser password. Verify an actual instrument read through your Modbus client. Web success and an open TCP port do not replace data checks. See the [user guide](user-guide.md).

On installer failure, RNG error or missing CF, do not delete state/witness/markers or blindly repeat production-enable. Retain non-secret installer/RNG status and use the [diagnostic guidance](rng-autonomous-v1.md).
