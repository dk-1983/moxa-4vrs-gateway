# v2026.02.01 — release scope and final review bundle

[Русский](release-final.ru.md) · [Commissioning](release-final-commissioning.md) · [No-op investigation](release-final-noop.md)

**Ready for release within the stated limited scope; mandatory gates are closed.** Historical equal-plan repair is attributed to platform verify; the diagnostic experiment identified network-service-health, followed by result3/no-op retaining Gateway/RNG PIDs and generation. Unchanged Web PID is not established. [Hardware evidence](release-final-noop-hardware.md). The historical network root cause remains unproven.

| Check | Result and limits | Evidence |
|---|---|---|
| Working CF, 20 stop/start | PASS on preceding RNG slow-commit candidate | Operator confirmation |
| Hour of HTTP/resources | Completed; two HTTP outliers retain unknown cause | Operator confirmation; GET load is separate from password KDF |
| web-ipc-v1 power loss after completed commit | **10/10 PASS**, user reduced 20 to 10; generations33–42, reused PID945/socket without intervention | [Project report](forensics/moxa2-web-ipc-power-cycles-20260911.ru.md), read locally |
| LAN1 down/up | One PASS: Gateway945/RNG generation42 retained, Web restarted, HTTP200 and P1/P2 TCP recovered | [Project report](forensics/moxa2-web-ipc-lan-return-20260911.ru.md), read locally |
| Spare CF FAT32 → ext3, files, explicit schema1→3, installation | PASS using a separately corrected local wizard copy; result0 verify-installed, configuration preserved | Operator confirmation; referenced desktop files not locally available yet |
| Spare CF, three restarts | PASS, generations5/6/7, HTTP200, whole-cycle 12.484/11.594/11.578 seconds, P1/P2 TCP in every snapshot | Operator confirmation; not a Modbus loss measurement |
| Healthy repeat; historical repair | **PASS focused no-op:** result3, healthy, count0, Gateway/RNG PIDs and generation retained. Historical FAIL remains: equal plan → platform verify failure; new repair → network-service-health | [Separate findings and Web PID caveat](release-final-noop-hardware.md) |
| Old wizard ZIP da91…f12f1 | FAIL: helper pinned f519… while nested archive was 92e3… | Preserved source/package; old ZIP retained |
| New distributed ZIP | Actual unzip, both manifests, SHA256SUMS, package.load, executable worker refusing missing arguments, corrupt archive refusal | delivered-kit.json; no media opened |
| Changed installer | normal/UBSan no-op, justified repair, unhealthy repair, diagnostic reads, journal lock/corruption refusal, target ABI/package | validation.json |

Hardware identity: Moxa #2 **192.0.2.15:22**, MAC **02:00:00:00:00:02**. Spare CF UUID **d4a46714-7f07-4fbf-ae88-4fb6e709512b**. Working CF preserved separately. No old-card RNG image was copied. A fresh input was physically delivered on CF, used once and removed. The historical UUID Bash script returned 00 instead of 7f and is not authoritative; embedded RNG pread remains unchanged, while Perl sysread matched PC UUID according to the operator.

Old candidate cycle2 remains FAIL. New web-ipc-v1 cycle2 PASS is a different experiment. Post-startup GET times 95.563–143.709 ms are not boot times. LAN observations at 15-second intervals do not establish exact recovery latency.

The supported evidence scope is autonomous Web/Gateway/RNG startup on the specified device with healthy consistent CF after completed commit, P1/P2 TCP reconnection, one LAN recovery and explicit fresh-entropy commissioning. Production-autonomous-v1/schema3 persists the successor before output; eight is diagnostic, ordinary startup needs no service-arm, and ambiguous persistence closes RNG/Web without automatic retries.

Physical power loss inside NV write/fsync, CF/NAND endurance, physical NAND write counts, eight-port hardware load and lossless Modbus are unqualified. These hardware series also do not establish HTTPS/authentication, DHCP/DNS/address changes or all media failures. Existing local HTTPS/authentication tests remain local evidence. These exclusions do not create new mandatory series for this limited release; broader claims require separate qualification.

Mandatory gates are closed: coherent wizard/hashes, explicit commissioning, compatible package, configuration/RNG preservation, agreed autonomy series and focused healthy no-op. Splitting ready/settled/error is separate diagnostics if unexpected repairs recur, not a blocker for this scope. No automatic first activation was added. Publication is separate; no devices changed in this task.
