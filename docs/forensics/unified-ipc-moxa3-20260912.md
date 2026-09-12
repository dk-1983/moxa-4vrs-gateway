# Unified IPC path: Moxa #3 hardware check, 2026-09-12

[Русский](unified-ipc-moxa3-20260912.ru.md)

Both target OS profiles use `/var/4vrs-web-ipc`: directory 0711, socket 0666, lock 0600. Ownership, inode and active-endpoint checks remain intact.

On Moxa #3 (UC-7420-LX Plus, vendor OS 1.6, Linux 2.6.10), `/var` is RAM-backed ext2 and `/tmp` is persistent JFFS2. Moving IPC avoids these temporary filesystem operations on internal flash; it does not eliminate other flash/CF writes. Existing `/tmp` objects were not removed.

Candidate v2026.02.03 unified-ipc: 1,151,223 bytes, SHA256 `ab77cee576abbf8bf2e63ec28f04938cc4a36584184612897812a6cfb578c081`. Installation completed with result=0, stage=verify-installed. Four installed application binaries matched the archive by SHA256.

HTTP returned 200 before and after a clean stop/start. Observed response times were 0.031 s and 2.047 s for the first request after restart; these are individual observations, not performance statistics. The old socket disappeared on stop and the new socket appeared under `/var/4vrs-web-ipc`, confirmed through `/proc/net/unix`. Empty lock files remain in RAM until reboot.

RNG remained ready/schema3: generation 16 before installation, 17 after installation, 18 after stop/start. No reactivation or manual marker deletion occurred. Configuration, network interfaces and confirmed network profile retained their hashes. Gateway was left running.

Host HTTP/TLS integration and SIGKILL/stale-endpoint recovery passed, including preservation of a foreign object. Physical power cycling, device authentication and UART traffic were not tested in this run. Moxa #1/#2 were not updated. Linux 2.4 has a separate limited Web smoke result; its full installer remains unqualified. Nothing was published.
