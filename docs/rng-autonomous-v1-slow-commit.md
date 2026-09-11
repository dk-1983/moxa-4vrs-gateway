# Slow NV commit: cause and fix

[Русский](rng-autonomous-v1-slow-commit.ru.md) · [Moxa #2 recovery](rng-autonomous-v1-moxa2-recovery.md)

The hypothesis is confirmed **locally on the previous autonomous candidate** using
real Gateway → RNG client → broker. Moxa #2 was not accessed; its current service-required
state cannot be attributed to this cause without hardware evidence.

A host-only LD_PRELOAD fixture delays fsync by 4500 ms after writing pending or
witness-next. It records monotonic timing, PID, signal, exit status and SIGTERM
handler/mask only. Seeds are public disposable fixtures; eight UART transports are mocked.
This is delayed-I/O simulation, not physical CF qualification.

| Previous candidate | Delay | Broker termination after delay begins | Outcome |
|---|---:|---:|---|
| normal pending | 4500 ms | 2996 ms | signal 15; service-required |
| normal witness-next | 4500 ms | 3006 ms | signal 15; service-required |
| UBSan pending | 4500 ms | 2994 ms | signal 15; service-required |
| UBSan witness-next | 4500 ms | 2999 ms | signal 15; service-required |

The test asserts the causal chain: client closes an unanswered request at 3000 ms;
Gateway calls rng_stop and sends SIGTERM; the old broker installs handlers only
after rng_nv_start. At delayed fsync SIGTERM is unblocked with default disposition.
Gateway waitpid confirms signal 15 before the delay ends, no RNG output and status 8.

The fix:

- Allow up to 30 seconds for the first RNG response byte after a fully sent request.
  Partial request transmission, a response already in progress and attach retain
  three-second bounds. Tests cover exact 29999/30000 ms and partial-response boundaries.
- Block SIGTERM/SIGINT across Gateway fork/exec; install broker handlers before NV
  I/O, then unblock. Pending early termination starts no new write.
- Block these signals throughout persist so they cannot interrupt write/fsync.
  Restore the mask after the outcome is known. Handlers only record stopping;
  real I/O failures are never retried.
- Check stopping after rotation before output. Finish an in-progress commit and
  exit without output when termination was requested.
- Latch Gateway failure immediately on timeout: close Web without signal/restart
  loops; keep main Gateway and ports running.
- Disable the service input alarm before saving; its 30-second limit still covers
  receiving fresh external randomness, not the commit.

Normal/UBSan tests pass: 4500 ms leads to Web readiness after commit; explicit
SIGTERM during commit leaves consistent state without output; 32000 ms exceeds
the client deadline but commit completes around 32004 ms and later status is ready.
Early termination before main and termination during rotation also pass: 1024 old
requests served, next generation committed, no 1025th reply after stopping.

Durable-save-before-output remains mandatory. SIGKILL, real power loss, faulty CF
and indefinitely blocked I/O remain hardware risks. A stuck broker may outlive
client failure; no automatic SIGKILL or repeated write occurs. Full Gateway shutdown
may wait for it and requires separate diagnosis rather than forcibly breaking commit.

The installer also now accepts valid 160-byte schema 1 and 192-byte schemas 2/3.
Application installation/rollback never includes RNG files in its write plan or
removes incomplete transactions. Corrupt state still refuses before process stopping.

Run tools/reproduce-rng-slow-commit.py with baseline/fixed and host/ubsan in the
existing moxa-dev-20260909-064851 container; rotation host/ubsan checks stopping at
rotation. Build/tests use tools/qualify-rng-slow-commit.py. Baselines use preserved
old executables. Target ELF contains no delay probe. Only nonsecret JSON evidence
is exported, never fixture state or logs.

Successful commit write frequency is unchanged; the reproduced artificial
three-second interruption is removed. The two historical HTTP outliers retain an
undetermined cause. Web load and separate password verification remain distinct.
