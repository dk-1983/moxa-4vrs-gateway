# Repeat installation: no-op versus repair

**Ready for release within the stated limited scope; mandatory gates are closed.** Historical equal-plan repair is attributed to platform verify; the diagnostic experiment identified network-service-health, followed by result3/no-op retaining Gateway/RNG PIDs and generation. Unchanged Web PID is not established. [Hardware evidence](release-final-noop-hardware.md). The historical network root cause remains unproven.

[Русский](release-final-noop.ru.md) · [Release matrix](release-final.md)

Hardware no-op failed: repeating the same installer from gateway-92e35241d9ee8d3f ended result0 verify-installed but changed Gateway/RNG/Web PIDs, generation3→4 and reconnected P1/P2 TCP. Unchanged gateway.conf does not establish equality of the complete managed set. The harness initially waited only three seconds; a restart attempt was refused by the lock while Gateway stayed alive. Waiting was corrected to terminal result without rerunning installation. The PID/generation change is independently confirmed.

The orchestrator compares every plan member's kind, mode, size and bytes. Besides ELF/configuration this includes interfaces/resolver, confirmed/good, vendor/managed scripts, clock marker, startup gates and links. Equal files also require target health: Apache state, Gateway identity/readiness, supported network observation, ready/settled/error-free network owner, DHCP lease and applicable baseline address/netmask/broadcast/up/default route/DNS checks.

Only complete equality plus health yields result3 already-installed without stop/start. A recovery-executable-only change is repaired without service restart. Result0 verify-installed with new PIDs therefore means a transactional repair: plan mismatch or failed platform verify. The old result did not retain the branch; subsequent matching-journal inspection established equal files and failed platform verify. The narrower historical cause remains unproven.

After coordinated staging of the verified new installer on Moxa #2, first read retained evidence from its package directory:

```sh
./4vrs-install --decision-journal
./4vrs-install --status
cat /etc/4vrs-installer/active
```

Do not run the argument-free installer to diagnose the old event: it may replace the journal. The new read-only command acquires a nonblocking shared lock on existing install.lock and decodes both retained slots with the production checksum/path allowlist. It creates nothing, performs no recovery/service action and reads no CF/RNG. Output contains only paths and before/after kind/mode/size/content_changed, never contents or hashes. Busy lock or corrupt journal is refused.

Associate the slot with active and the old result. A differing member identifies the exact path/metadata; investigate content changes locally against the responsible generator without exporting secrets. An entirely equal journal establishes the platform-verify branch, but cannot recover its precise historical reason. Never present a replacement journal as the original one.

Future results retain decision, decision_health, decision_count, decision_path and decision_mask. First-difference mask: 1 kind, 2 mode, 4 size, 8 unequal bytes at equal size. Decisions: no-op, repair-plan-differs, repair-platform-unhealthy, repair-recovery-only. As before, health is skipped for a differing plan. Health failures distinguish application-identity/readiness/absent, apache-inspection/running-state, network-observation/service-health, dhcp-lease, lan1/lan2-baseline and default-lan/gateway/dns-baseline. Final successful verify cannot overwrite the original decision reason.

No health checks were relaxed. Local normal/UBSan covers healthy equal files, a missing managed clock marker and unhealthy equal files, plus read-only metadata output, lock exclusion and corrupt-journal refusal. Future hardware observation must wait for terminal result; result100 is still running, not success or permission to relaunch.

**Gate closed by hardware experiment on 2026-09-11.** See [results](release-final-noop-hardware.md). Individual network signals may be investigated separately; no further installation or power series is required for this release.
