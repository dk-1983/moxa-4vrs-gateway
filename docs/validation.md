# Validation status

English · [Русский](validation.ru.md)

This is a public-facing summary of development evidence, not a release
certificate. The first public `v2026.00.00` package has not yet completed
qualification. Development candidate **r15** is installed on two
UC-7420-LX Plus devices; that does not establish compatibility with other models.

## Recorded evidence

| Area | Evidence and limit |
| --- | --- |
| Integrated r15 build | 20 host suites, four relevant UBSan suites, full XScale build and target ABI audit reported for r15 |
| Both devices | Installation and a software reboot recorded on each device; saved device-specific configuration retained |
| Startup recovery | Repeated starts and restart exercised; optional absent vendor interface and loopback startup defects reproduced and corrected |
| Idle power interruption | One r15 power cycle recorded on the first device; this is not interruption during a configuration write |
| Static network UI | LAN edits, explicit Keep/Revert and timeout rollback exercised; mask Apply/timeout rollback also observed with management access and sampled serial reads |
| DHCP | Selected acquisition, renewal, expiry, policy recovery and Revert scenarios exercised on an isolated LAN; this is not complete DHCP interoperability coverage |
| Clock | Earlier RTC UTC write/readback and real NTP diagnostic attempts recorded; operator observed automatic synchronization after r15 boot on both devices |
| Serial transports | Earlier per-mode instrument/client and RAW loop tests recorded; r15 first-device checks include sampled Modbus reads after boot |
| Isolated crash harness | Four cases executed on target OS/CompactFlash: interruption before/after publication, peer loss during Apply, and writer timeout; expected private-store recovery observed |

The crash harness uses production components with synthetic LAN providers and
a separate test store. It did not stop the installed Gateway or cut physical
power. Its results do not replace complete application/owner-path tests.
Likewise, a TCP connection on the second device is not an instrument-read pass.

## Remaining release work

The internal qualification matrix still has open entries for physical write
interruption and further deployed crash phases; actual DNS resolution after
configuration changes; automatic DHCP DNS lifecycle; remaining DHCP ownership
and cancellation cases; alternate route/mask behavior; and explicit listener
affinity through address and lease transitions. Each needs its stated evidence
or an explicit, documented scope decision before release acceptance.

The public installer, clean source export, final version labeling, release
artifact build and checksums also remain unfinished. The final package must be
checked against its own source and contents; the earlier candidate's tests do
not automatically qualify a different binary or installer.

Extra endurance runs and exact NTP latency measurements are separate from the
recorded functional results. No multi-day soak, universal server compatibility
or industrial safety certification is claimed.

## Reproducing checks

Host test runners are under `tools/run-host-*-tests.sh`; target build scripts
are under `tools/build-*.sh`. Obtain the vendor toolchain independently and
follow the [build environment](architecture/build-environment.md). Tests that
need device access or archived local evidence are not ordinary host checks.
Do not execute target helpers against operating equipment without reviewing
their action, configuration and recovery requirements.

When reporting additional results, identify the exact artifact, device model,
fixture, operation and observed outcome. Preserve the distinction between
simulation, target component tests and complete live-device behavior.
