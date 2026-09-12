# web-ipc-v1 hardware qualification — Moxa #2

> Current [release matrix](release-final.md). The user reduced the series to 10; 10/10 completed. Hardware no-op acceptance is closed; ready for release within the matrix scope. No power-series repeat is required.

[Русский](web-ipc-power-cycle-plan.ru.md) · [Fix and evidence](web-ipc-lifecycle.md)

The original plan below is retained for traceability. The user reduced the required series to 10; 10/10 completed. It does not require a repeat. Current gates are in the release matrix. Device access, installation and power changes require separate authorization. The only target is Moxa #2 **192.0.2.15:22**. Moxa #1 **192.0.2.13:622** is excluded and must never be a fallback. No firmware update is required.

## Preparation

1. Use the new full `4vrs-gateway-v2026.02.01-web-ipc-v1-candidate.tar.gz`, verify its SHA256 against the bundle's `SHA256SUMS`, and review validation.json and ABI evidence. The CF wizard's nested gateway.tar.gz must be byte-identical. Preserve the previous rng-slow-commit-v1 installer SHA256 `8e9fd95f2dd8392dea0adde301c69343a7a9443ccb2215714e7d0e5537a34978` and reversible application-update path. Never roll RNG state back with application binaries.
2. Once separately authorized, record nonsecret baseline data: explicit address/port, versions and binary hashes, Gateway/Web/RNG PIDs, Web mode/ports, P1/P2 traffic, `/tmp` filesystem and permissions. Use RNG `status`; do not export seed, state/witness, passwords or keys. Consistent ready state does not require service recovery.
3. Install through the normal reversible application installer, preserving operational mbusd, configuration and RNG. Do not remove or rename RNG markers. Preserve `/tmp/4vrs-web-945.sock.cycle02-preserved`. Do not reactivate RNG or reprepare an already healthy CF.
4. Check initial startup: `/var/4vrs-web-ipc` is a directory with mode 0711 and Gateway UID/GID; current `<PID>.sock` is a socket with mode 0666; `<PID>.lock` is an empty regular file with mode 0600; the exact socket path appears in `/proc/net/unix`. Check HTTP 200, configured HTTPS mode, authentication and real P1/P2 traffic. Keep passwords/tokens/cookies out of reports. Capture communication error/counter baselines.

## Historical original plan: 20 (agreed and completed: 10)

1. Before each cycle 01–20, record PIDs, RNG generation/status, Web state/error, exact socket path, P1/P2 counters and time. Confirm completed startup commit through ready status; incomplete NV commits belong to a separate fault series.
2. Physically remove power from the whole device without stop/reboot. Record off duration and restore power following the agreed bench procedure. Application stop/start does not substitute for this step.
3. From power restoration, measure time to Gateway, RNG ready and first HTTP 200. Allow up to 180 seconds of observation for this series; record actual delay and retries, not just PASS. Check Web process and matching Gateway socket/proc entry. Use bench monotonic time because device clocks may change.
4. Verify real P1/P2/Modbus traffic after every startup and compare errors to baseline. Record only the nonsecret RNG generation: no rollback, mandatory service-arm on consistent CF, or blocking at generation eight. Check configured HTTP/HTTPS and authentication separately from GET load and KDF operation.
5. PASS requires autonomous Gateway/RNG/Web and restored traffic without intervention. Intervention, missing Web, RNG rollback/ambiguity or lost ports means FAIL. Preserve path type/mode/inode, exact proc entry, PIDs and statuses; never delete files to obtain PASS. Stop the series after a failure for investigation. Previous stop/start tests and cycle 1 do not count toward the new series. Previous cycle 2 remains FAIL despite HTTP 200 after renaming.

## Separate scenarios and acceptance gates

An autonomous power-recovery release claim is blocked by the incomplete series and missing target validation of JFFS2/proc/permissions. Also verify repeated PID/name with a safely created orphan in the new namespace on an isolated bench. Local regression already covers this; never replace an active hardware endpoint or terminate Gateway without separate authorization.

RNG fault qualification is separate: power loss at each NV transaction boundary, corrupt/unavailable/full CF, ambiguous writes and absence of automatic retries while real ports continue. See the [RNG hardware plan](rng-autonomous-v1-hardware.md). Twenty ordinary power cycles do not prove these scenarios or CF endurance; they remain gates for a claim of full power/CF fault coverage.

Network qualification is separate: link/address/DNS loss and recovery with ports and Web active, plus prolonged GET load. Two HTTP outliers retain their unknown-cause status. None of these substitutes for password verification in the separate KDF process. Measure JFFS2/CF writes under normal operation and repeated failures; generation counts do not establish physical NAND writes.

If RNG becomes service-required, use the [RNG recovery plan](rng-autonomous-v1-moxa2-recovery.md) only after separate analysis and authorization. An IPC leftover alone is not grounds for RNG recovery. Never include RNG contents or external entropy in public evidence. Publication requires a separate decision after qualification.
