# Initial commissioning: CF → install → explicit activation → Web

[Русский](release-final-commissioning.ru.md) · [Release matrix](release-final.md)

This is a procedure for a separately coordinated maintenance window, not an operation performed here. Target only Moxa #2 192.0.2.15:22, MAC 02:00:00:00:00:02; never fall back to another device. The wizard prepares CF; it is **not fully automatic commissioning**. Combining installation/activation is separate development; no hidden regeneration was added.

1. On Ubuntu 24.04 x86-64, verify the distributed ZIP SHA256. Extract using unzip into a new root-owned PC directory outside CF:

   ```sh
   sudo install -d -m 0755 /opt/4vrs-v2026.02.01-final
   sudo unzip 4vrs-cf-wizard-v2026.02.01-final-review.zip -d /opt/4vrs-v2026.02.01-final
   cd /opt/4vrs-v2026.02.01-final
   sha256sum -c SHA256SUMS
   sudo python3 cf-wizard.py --list
   sudo python3 cf-wizard.py --report /root/cf-v2026.02.01-new.json
   ```

   Confirm physical card, target MAC and destructive formatting only for the authorized new/spare CF. Verify the new ext3 UUID directly on PC against the report. Preserve the working card separately; never copy its RNG. Historical Bash UUID output is not authoritative. Worker file verification ends commissioned=false, activation pending.

2. New schema1 requires a separate **fresh 32-byte input** from the trusted Ubuntu PC CSPRNG, excluded from reports/ZIPs. After wizard completion, mount the same verified CF at root-owned /mnt/4vrs-cf with nosuid,nodev and verify device/UUID using findmnt/blkid. The tested spare UUID is d4a46714-7f07-4fbf-ae88-4fb6e709512b; other new cards have their own UUID. Example one-time preparation, not executed here:

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

3. On the device select `4vrs-packages/gateway-<first 16 SHA256 characters of gateway.tar.gz>` from the new SHA256SUMS. Verify MAC/UUID/configuration. Run `./4vrs-install` once and poll `./4vrs-install --status` until terminal result. **Result100 is still running**; result0 verify-installed means installation completed, result3 already-installed means no-op. Investigate other results; a three-second timeout does not authorize relaunch. Before policy activation Web may remain closed.

4. After terminal result, coordinate `/etc/init.d/4vrs-gateway stop` and confirm the RNG owner exited. For new consistent schema1 run **once**:

   ```sh
   /var/hda/4vrs/bin/4vrs-rng production-enable /var/hda/4vrs-rng --fresh-entropy-confirmed < /var/hda/4vrs-service/fresh-rng-input
   ```

   On failure stop; service-recover is not automatic fallback. On success verify `4vrs-rng status /var/hda/4vrs-rng` reports ready/schema3, remove only the consumed fresh-rng-input and complete filesystem persistence. Never remove RNG state/witness/markers or reuse the input. Healthy active schema3 requires no activation.

5. Run one normal `/etc/init.d/4vrs-gateway start`; verify Gateway/RNG/Web, generation, configured HTTP200 and P1/P2 TCP. Check HTTPS/authentication separately when needed. Confirm gateway.conf preservation without exporting its contents. RNG ready is not system readiness; TCP connectivity is not proof of lossless Modbus. Subsequent ordinary restarts on consistent CF are autonomous.

The operator confirmed separate spare-card preparation, initial activation, installation and checks. This does not establish automatic end-to-end commissioning by the wizard. The procedure above separates installation and explicit policy transition from normal runtime. Do not repeat activation on the already commissioned card.
