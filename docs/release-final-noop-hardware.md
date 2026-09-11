# Hardware no-op acceptance — 2026-09-11

[Русский](release-final-noop-hardware.ru.md) · [Release matrix](release-final.md)

Source: the operator's supplied hardware diagnostic account. Raw desktop snapshots were not independently read in this task. Target: Moxa #2 192.0.2.15:22, MAC 02:00:00:00:00:02, CF UUID d4a46714-7f07-4fbf-ae88-4fb6e709512b.

| Experiment | Established | Not established |
|---|---|---|
| Historical journal before new installation | Diagnostic installer staged only in /dev/shm; active slot0 and result utc1789141385 match the recorded repeat operation. Slot0 valid, differences=0, was_running=1; slot1 contains five first-install creations. Reading preserved processes and generation7 | Historical repair was triggered by platform verify with an equal plan; the precise historical health code was not retained |
| First diagnostic-package installation | Archive SHA256 624f807edb127977e09dde918460653b32b2b6f3590869080a6c144e6478844c and transferred payload verified. result0 verify-installed; decision=repair-platform-unhealthy, decision_health=network-service-health, decision_count=0 | Establishes this repair reason, not the old event's precise cause. The code combines service query failure, missing ready/settled or nonzero service error |
| Exactly one repeat after terminal result | result3 already-installed, decision=no-op, decision_health=healthy, count0. Gateway/RNG PIDs and RNG generation retained | Web was absent from early repeat-before and appeared as PID3637 in repeat-after; this pair does not prove unchanged Web PID |

Operator's raw snapshot references: `../moxa2-final-install-20260911/repeat-before.txt` and `repeat-after.txt`. HTTP and P1/P2 TCP were checked separately; Modbus transaction quality was not measured. The power series was not repeated. Gateway/Web/RNG/KDF match the unchanged candidate contents. Health checks were not relaxed.

**Healthy hardware no-op acceptance is closed.** The historical FAIL remains recorded, its branch is now established, and the planned focused experiment confirmed correct no-op. Further splitting network-service-health into ready/settled/error is not mandatory for the stated limited release: healthy repetition was accepted, an unhealthy installation was not wrongly accepted as no-op, and the observed repair completed verification.

Inspect individual network health signals as a separate diagnostic task if unexpected repairs recur or their frequency needs explanation. This cannot retroactively establish the historical network root cause and does not require expanding this release. NV/power/CF, eight real ports and Modbus loss exclusions remain. Publication is separate; no device changes were made in this task.
