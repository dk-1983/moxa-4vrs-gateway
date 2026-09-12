# Web IPC after power loss — web-ipc-v1

The v2026.02.03 candidate uses `/var/4vrs-web-ipc` in RAM on both target OS families. Historical results below concern the previous path; they do not constitute a repeat qualification of this candidate. Installation, HTTP and stop/start were verified on Moxa #3; the physical power-cycle series was not repeated for this change. Old objects in `/tmp` are not removed automatically.

> Current [release matrix](release-final.md). The user reduced the series to 10; 10/10 completed. Hardware no-op acceptance is closed; ready for release within the matrix scope. No power-series repeat is required.

[Русский](web-ipc-lifecycle.ru.md)

Candidate v2026.02.01-web-ipc-v1 fixes UNIX socket name reuse after abnormal termination. `/tmp` is not assumed to be RAM: Moxa #2 uses persistent JFFS2. RNG code, markers, seed-before-output ordering, HTTP/HTTPS, authentication and the separate password-verification process remain unchanged.

## Cause and hardware observations

For the previous rng-slow-commit-v1 installer (SHA256 `8e9fd95f2dd8392dea0adde301c69343a7a9443ccb2215714e7d0e5537a34978`), the operator reported 20 successful stop/start tests and an hour of GET requests. Physical cycle 1 passed; cycle 2 **failed autonomous startup**. RNG was ready at generation31, Gateway/RNG and P1/P2 remained alive, but Web was absent. Gateway reused PID945 and `/tmp/4vrs-web-945.sock` survived without a listener in `/proc/net/unix`. The old bind failed, producing Web error4 and a retry every 60 seconds; cleanup conditioned on listener >= 0 also missed partial startup.

After checking the object type, absent destination and absent listener, the operator renamed only that socket to `/tmp/4vrs-web-945.sock.cycle02-preserved`. Without restarting Gateway/RNG, the regular retry returned HTTP 200 in **0.096739 seconds**. This supports the IPC diagnosis; manual recovery does not make cycle 2 a PASS.

These hardware facts come from the operator's message. The supplied desktop location `outputs/moxa2-rng-slow-commit-20260911/power-cycles`, including `cycle-02-after-ipc-intervention.txt`, was unavailable at the known local desktop path and was not independently read in this review. No additional PID or measurement is inferred. The two earlier HTTP outliers remain separate observations with an undetermined cause.

## Ownership protocol

1. Gateway creates `/var/4vrs-web-ipc` with exact mode 0711 and its UID/GID. Every ancestor must be a real directory with a trusted owner; other-user write permission requires the sticky bit. Symlinks, foreign ownership or unsafe modes fail without repairing the object. O_DIRECTORY/O_NOFOLLOW and matching device/inode checks verify the opened directory.
2. `<PID>.lock` protects `<PID>.sock`: an empty regular file, mode 0600, one hard link, correct UID/GID, no symlink following. A nonblocking `fcntl(F_SETLK)` lock is held throughout endpoint lifetime. Descriptors have FD_CLOEXEC. A competing process cannot take the endpoint.
3. A leftover is removed only if it is an owned socket with one hard link, its exact path is absent from successfully read `/proc/net/unix`, and a repeated device/inode check matches. Bound but not listening sockets are also treated as active. No connection probe is made to another service. Proc read failure prevents deletion. Regular files, directories, symlinks and active sockets are preserved.
4. Successful bind immediately records inode identity, before chmod 0666, listen and nonblocking mode. Failure at a later stage cleans up only this inode while holding the lock, even before Gateway receives the listener. A replacement object is neither deleted nor chmodded during cleanup. Cleanup failure leaves the remainder for a subsequent complete safety check; there is no unconditional removal.
5. Normal shutdown removes only the owned inode. The old `/tmp/4vrs-web-*.sock` namespace, including the preserved hardware artifact, is untouched. Lock files remain intentionally: deleting them would break exclusion against processes holding the old inode.

The trust boundary excludes hostile root or same-UID processes, which can modify the protected directory. Untrusted Web and other unprivileged users can traverse mode 0711 to reach the mode 0666 socket but cannot rename it. Existing `SO_PEERCRED` verification of the expected PID and UID is preserved. IPC failure keeps Web unavailable with existing error4/retry behavior while Gateway and ports continue. RNG failure independently closes Web and prohibits automatic NV retries after ambiguous persistence.

The implementation uses Linux 2.6.10/libc APIs, without openat, SOCK_CLOEXEC or a tmpfs assumption. ABI checks use preserved XScale big-endian libraries. Target JFFS2, proc and permission behavior still require hardware qualification.

## Writes and limits

Full regression also exposed a Disable/Enable race: `rng_stop` closed channels before SIGTERM, allowing broker EOF exit22 before the signal and Gateway error52. Gateway now sends SIGTERM before closing channels. `test_rng_stop_order.c` calls the actual stop function with simulated immediate EOF handling: previous code reproduces the failure in normal/UBSan; fixed code passes. This validates syscall ordering and does not reinterpret hardware cycle 2. Broker/client/NV sources, persist-before-output and ambiguous-commit handling remain unchanged.

The directory is created once; a new PID creates one empty lock, reused when that PID repeats. Successful startup creates and chmods a socket; normal shutdown unlinks it. Following a crash, a stale socket is removed only after the next complete safety check. These are persistent `/tmp` metadata operations on internal JFFS2, not additional RNG writes to CF. They do not establish a physical flash-write count. Lock files accumulate for used PIDs; no wildcard garbage collection is performed. Refusing a foreign/active endpoint does not alter it, although a new lock can be created before refusal.

The [autonomous RNG policy](rng-autonomous-v1.md) and [slow-commit fix](rng-autonomous-v1-slow-commit.md) are retained: persist the successor before output; eight generations is diagnostic, not a proven CF endurance bound. Ambiguous persistence requires separate RNG recovery; a stale Web socket alone does not.

## Local validation and remaining gates

`test_web_ipc_endpoint.c`: 99 assertions in normal and UBSan cover crash leftovers/reused name 945, a competing process, listening and bound-only sockets, regular files/symlinks/directories, foreign UID, failures after bind/chmod/listen/nonblock, cleanup/proc failures, replacement before cleanup and unsafe directories.

`test_gateway_ipc_restart.py`: actual Gateway/Web/RNG processes, SIGKILL of the fixture process tree, retained inode and reused endpoint name; HTTP 200, successor RNG generation and eight mock ports in normal/UBSan. This is neither a physical power cycle nor a UART test.

The same test confirms that a foreign file produces error4 and remains unchanged while Gateway progresses, RNG stays ready and eight mock ports remain active. Target 4vrs-rng, 4vrs-web, 4vrs-kdf and 4vrs-install are byte-identical to slow-commit-v1; Gateway changed. Slow commits of 4.5/32 seconds and stop during rotation were retested with final Gateway in normal/UBSan.

The bundle includes validation.json, ABI, inventory and SHA256SUMS. The agreed 10/10 hardware series is complete and needs no repeat. Closed no-op acceptance and excluded in-NV power/CF endurance/Modbus loss claims are listed in the [current release matrix](release-final.md). The original plan is retained for traceability; no devices are changed in the current task.
