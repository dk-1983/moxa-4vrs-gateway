# Validation scope — v2026.01.00

[Русский](validation.ru.md) · [Release notes](releases/v2026.01.00.md)

This release uses the unchanged tested installer/Gateway package. The owner
authorized publication with the untested boundaries below explicitly retained.
This is not complete hardware qualification or universal vendor-layout support.
v2026.00.01 remains unchanged. No device update is implied by publication.

| Area | Evidence and scope |
| --- | --- |
| Host/build | 22 host suites; relevant UBSan for application, panel, persistence, clock provider and installer; 9 packaging tests; both complete XScale ELF ABI audits |
| Managed update | Actual native installation, own network/serial/NTP/store preservation, healthy repeat without service restart or installed-file rewriting |
| Backlight | Operator observed physical On/Off, menu navigation while Off, persistence, one normal reboot with saved Off, then return to On |
| Immediate recovery | One instrumented exact publish-application process interruption followed by independent recovery and original recovery-ELF repair, CF available |
| Boot recovery | One separately authorized reboot after that exact instrumented boundary, without manual recovery beforehand; CF available, valid rollback generation, before-set restored, automatic services, TCP3/3; stale worker result100 is not a new boot result |
| Native observer | Positive target observation passed; stale/reused/zombie/live/error cases additionally covered locally, not all separately on target |
| Earlier network/clock/serial | Selected static LAN/mask/Keep/Revert, DHCP acquisition/renewal/expiry/policy recovery, RTC/NTP, instrument reads and RAW loop experiments on earlier product revisions; retain each experiment's scope, not a new full release cycle |

Instrumented interruption uses original production orchestration/recovery with
a qualification-only boundary. It is not a physical power cut or proof of all
crash phases. A TCP connection is not a Modbus/instrument read; the latest second
device trials used TCP checks. Console reboot capture was not a full early-boot
trace. Source/artifact and reviewed evidence hashes are in the
[release evidence manifest](releases/v2026.01.00-evidence.json).

## Explicitly untested / deferred

- Real first installation on a system without Gateway; deliberately absent or
  late CF; physical power removal during a durable write remain hardware-unverified.
  The owner deferred scenarios needing recovery media because the only two CF
  cards are in working devices. A spare card purchase, destructive teardown or
  factory reset is not required for this release. These tests are not called passed.
- Further DHCP cancellation/late ACK, NAK/conflict/foreign-client and independent
  LAN/both-client target cases; actual DNS lookups after changes and automatic
  DHCP-DNS publication/withdrawal; alternate route/interface reachability;
  mask Keep/reboot or expanded-subnet behavior; dedicated listener-affinity and
  ordinary bind-edit transitions; remaining network durable Keep/crash phases
  retain their unproven hardware coverage. This is not a list of newly found bugs.
- Multi-day endurance, exact NTP latency/packet capture and additional RTC
  readbacks are optional extensions, not new mandatory tests for this release.

Later destructive or media-dependent tests require their own recovery preparation
and operator authorization. The web interface is deferred to a later release.
