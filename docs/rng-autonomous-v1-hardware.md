# Autonomous RNG: hardware qualification plan

[Русский](rng-autonomous-v1-hardware.ru.md) · [Policy](rng-autonomous-v1.md)

Not executed. Device access, installation, reboot and power control require separate
authorization. Moxa #2 is strictly 192.0.2.15:22; #1 is 192.0.2.13:622, never a fallback.

1. Record candidate SHA256, CF model/capacity, ext3/mount options, kernel and power
   observation method. Verify package/ABI and prepare application recovery without
   exporting live seeds.
2. On authorized test CF, qualify wizard preparation, one-time fresh activation,
   schema 3 witness verification and update without RNG modification. Test schema
   1/2 migration, ambiguous migration refusal, explicit recovery and old-binary refusal.
3. Load all eight Gateway ports and measure loss, errors and latency. Exercise
   HTTP, HTTPS, authentication and the separate password-checking process distinctly.
4. Perform at least 20 clean restarts including eight/nine without intervening
   arming. Check generation growth, Web readiness and Gateway continuity. Restart
   Web alone and verify that the broker can remain alive.
5. Perform at least 20 power cuts while normally serving after commit. Consistent
   post-ext3-recovery state must start without a PC, arm or isolation. Check no output
   reuse through a controlled test interface, without recording live secrets.
6. With a separately instrumented test build, cover before/after creation, write,
   fsync, close and rename of both records and directory synchronization. Repeat
   timed physical cuts with the ordinary build. Instrumentation is not delivered;
   NV_FAIL/NV_CRASH are absent from target executables.
7. Classify each outcome by reading. Exact matching records with no staging files
   permit a new successor; otherwise Web stays closed. Three repeated ambiguous
   launches and status polls must add no state writes/output. Never delete markers
   to pass a test. Exercise explicit fresh recovery.
8. Test short writes, ENOSPC/EIO, read-only/missing CF, loss during writing, corrupt
   state/witness and concurrent ownership. Never fall back to old state. Restoring
   a complete old CF image is unsupported.
9. Move RTC forward/backward and enable NTP: counters never decrease and eight
   does not block Web. Qualify the independent 60-second minimum rotation interval.
10. Measure commit counts, filesystem/block operations and fsync latency under idle
    operation, realistic Web load, separate password checks and repeated boots.
    Separate interrupted transactions (no later automatic state writes) from failures
    after commit (each successful reboot adds a commit). Do not infer NAND endurance
    from two 192-byte logical writes.
11. Separately test LAN loss/restoration, HTTP/HTTPS availability and reauthentication;
    compare ports and connection latency. Preserve the two HTTP outliers with an
    undetermined cause until reproducibly explained.
12. Export versions, errors, generations/counters, timings, aggregate I/O and public
    binary hashes only. Exclude state, witness, staging data, passwords, cookies and
    TLS private keys. Recheck report/package contents.

**Hardware release gates:** CF power/I/O durability, real-CF wizard/migration,
actual UART/Modbus continuity during RNG failure, and measured normal/fault write
frequency. Network cases remain separate untested Web/Gateway qualification items
for this candidate. Host/UBSan tests do not close hardware gates.
