# RNG: autonomous production policy

> Current local candidate includes the [slow NV commit fix](rng-autonomous-v1-slow-commit.md):
> up to 30 seconds for RNG, stop after commit outcome, installer compatibility with schema 3.

[Русский](rng-autonomous-v1.ru.md) · [Hardware plan](rng-autonomous-v1-hardware.md)

Local v2026.02.01 candidate: `production-autonomous-v1`, schema 3. Explicitly
replaces the serviced production-v1 model; never automatically promotes trial.
Normal startup needs no PC, service-arm, external entropy or 24-hour isolation.
No devices have been changed.

## Transaction contract

The 192-byte `4VRSNV03` record stores generation, cumulative commits, MAC/UUID
binding, successor seed, policy tag and SHA-256 corruption checksum. `witness`
contains an exact copy and is **secret**, just like `state`. Never export either.

Under the exclusive owner lock:

1. Require consistent state/witness and no pending, witness-next or legacy attempt.
2. Write the next record to witness-next; fsync, close, rename to witness, fsync directory.
3. Write the same record to pending; fsync, close, rename to state, fsync directory.
4. Permit RAM RNG output only after all operations succeed. The saved seed is the
   successor for a future startup, not a checkpoint of an already used DRBG.

The witness remains after commit and shutdown; only a new transaction replaces it.
Power loss while normally serving after commit therefore does not imply service.

| Read-only classification without a live owner | Result |
|---|---|
| Valid, identical state/witness; no staging files | Autonomously commit a successor before output |
| Staging files, missing/different witness, legacy attempt | Service required; no output or automatic repeated write |
| Corrupt state, invalid binding/metadata | Invalid state; never fall back to an older record |
| Another owner | Competing broker refuses owner-busy; original owner continues |

Any operation error stops output in the current process. Gateway closes Web and
latches automatic broker retries off; main Gateway and ports continue. A later
controlled launch first classifies by reading. Exact matching committed successor
and witness resolve a lost acknowledgement: advance again, never repeat the
disputed write. Unresolved records remain blocked through repeated launches.

Process-crash tests do not prove physical CF durability. Qualify Linux 2.6.10,
ext3, controller and actual CF under power loss. A complete old CF image with
matching records cannot be detected without an external trusted anchor. Never
restore old RNG images. Checksums do not protect against an administrator rewriting
both records and the binding.

## Operations and migration

`4vrs-rng status /var/hda/4vrs-rng` uses O_NOATIME, creates no lock, deletes no
files and performs no synchronization. Eight is `observation-threshold=8`, with
`threshold-reached=yes/no`: diagnostic only, not a rolling quota or endurance
guarantee. RTC/NTP does not reset counters. 32-bit generation/counter overflow
requires service.

Initial activation or clean schema 1/2 migration:
`production-enable PATH --fresh-entropy-confirmed`.
Ambiguous/corrupt state: `service-recover PATH --fresh-entropy-confirmed`.
Both require exactly 32 fresh external CSPRNG bytes on stdin. Never put entropy
in arguments, history or logs. Target PATH must be on verified `/var/hda/` CF.

Service requires separately authorized stopping of RNG consumers, media inspection
and a confidential channel. Valid previous successor is mixed with fresh entropy;
generation advances. Recovery also burns higher valid reservations found in witness
or staging records. With no valid records, fresh entropy establishes a new lineage,
not a restoration of old RNG. Compromise may additionally require revoking secrets
and sessions. No 24-hour wait is required. Old service-arm, service-resume and
isolation flags are rejected to avoid silently reinterpreting their contract.

Normal enable refuses ambiguous legacy attempt/pending; use explicit recovery.
Migration revokes the old policy before changing seed. Old binaries reject schema 3
without writing. Application rollback must never restore an old seed/CF image.

The CF wizard verifies schemas 1/2/3, requiring a matching witness for schema 3.
New CF still receives schema 1 with no enabled policy: perform **one explicit
activation** after installation. This is initial provisioning, not recurring
startup service. Package updates do not rewrite existing RNG state.

## Write frequency and limits

Each successful commit uses two 192-byte file writes, two file fsync calls, two
renames and two directory fsync calls. First lock creation, preparation, migration
and service add operations. Clean shutdown and status do not write state. These
are logical operations, not NAND program counts or guaranteed lifetime.

For R accepted broker requests per startup, estimate `max(1, ceil(R/1024))`
successful commits, plus provisioning. Requests remain at most 1024 bytes and
rotations at least 60 seconds apart. Premature rotation closes Web, independently
of the diagnostic eight-generation threshold. One quiet start/day means at least
365 commits and 730 content writes/year; 24/day means 8760 and 17520. Ten thousand
broker requests in a continuous run imply about ten commits.

An interrupted transaction starts at most two content writes; repeated ambiguous
launches add no state writes. Repeated failures **after successful commit** can
each cause another autonomous boot commit: there is no hard wear limit or guarantee
against an externally imposed reboot storm. Gateway itself does not automatically
repeat writes after a broker fault. Measure block I/O, ext3 journal and actual CF
under realistic boots/rotations; do not persist diagnostics per GET/status.

Measure Web load, RNG requests and the separate password-checking process
independently. HTTP/HTTPS, authentication and agreed connection delays are unchanged.
The two HTTP outliers retain an undetermined cause.
