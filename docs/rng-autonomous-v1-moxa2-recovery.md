# Moxa #2: service-required recovery procedure

[Русский](rng-autonomous-v1-moxa2-recovery.ru.md) · [Local cause and fix](rng-autonomous-v1-slow-commit.md)

**Plan only, not executed.** Use only Moxa #2 at 192.0.2.15:22. Never fall back to
#1 at 192.0.2.13:622. No device access or marker removal occurred. Reading, installing,
stopping processes, rebooting, CF removal and service writes need separate authorization.

1. After read authorization, verify identity, mount and binaries. Run read-only
   `4vrs-rng status /var/hda/4vrs-rng`; retain exit code and nonsecret output. Collect
   existence/size/owner/mode/times of state, witness, witness-next, pending, attempt,
   owner.lock and production-policy only. For a live broker, State/SigCgt/SigBlk/SigPnd,
   elapsed time and I/O counters are sufficient. Never dump/hash secret state for a
   public report, or export environ, cmdline, core, private keys or cookies. Do not
   remove/rename markers, promote pending, restore old images or repeatedly launch.
   Investigate a live D-state owner rather than sending SIGKILL. The local reproduction
   does not establish the cause on this particular CF.
2. Select the branch. Ready consistent schema 3 with no owner needs no fresh entropy:
   after updating, one controlled Gateway launch clears its RAM fault latch. Service-required
   with valid state needs the fixed full package and one explicit fresh recovery.
   Corrupt state/binding/permissions or mount/I/O errors require inspection first;
   do not normalize UUID/permissions blindly. Installer rejects corrupt state;
   a verified standalone helper may be staged only with separate authorization.
   Faulty CF requires replacement and fresh RNG lineage, not an old image restore.
3. Installer now accepts valid schemas 1/2/3 but never includes RNG files in update
   or rollback, never clears service-required or promotes trial. Schedule maintenance:
   application installation/restart does not promise uninterrupted ports. Ordinary
   RNG failure leaves main Gateway running.
4. Emergency recovery needs exactly 32 fresh bytes from a qualified external CSPRNG.
   Do not send them through the old SSH/console stack, command arguments, clipboard,
   logs or release archives. A physical-CF option uses an authorized clean shutdown,
   removal and verified Ubuntu topology/mount. On healthy CF create a separate 0700
   `/4vrs-service` directory and 0600 `fresh-rng-input` with O_EXCL/O_NOFOLLOW. Obtain
   exactly 32 bytes with getrandom(GRND_NONBLOCK); abort on short result/EAGAIN, no
   fallback/retry. Check full write, fsync file and directory, close and cleanly unmount.
   Do not touch `/4vrs-rng`. This is secret material on a trusted medium, never a
   report/archive input and never reusable. An already qualified confidential stdin
   channel without a file is preferable if available; none was verified here.
5. After separate mutation approval and corrected binary activation, ensure no RNG
   owner or competing startup. Use a controlled maintenance window, not lock deletion.
   With the prepared CF input, the future command on #2 is:

```sh
/var/hda/4vrs/bin/4vrs-rng service-recover /var/hda/4vrs-rng --fresh-entropy-confirmed < /var/hda/4vrs-service/fresh-rng-input
```

Only explicit recovery may process transaction leftovers according to its protocol,
burn valid higher reservations, mix fresh randomness and commit new state/witness.
No manual rm/mv. On failure preserve files and investigate; never automatically
repeat using the same input. No 24-hour isolation is required.

6. Verify ready/schema 3, absence of the old owner, then perform one controlled Gateway
   launch. Check Web, authorization and every working port. Exit zero alone is not
   full-system acceptance. Known valid generations advance; with no valid records,
   fresh randomness creates a new lineage rather than restoring old RNG. Compromise
   may require separate secret/session revocation.
7. Retire the service input through separately authorized confidential cleanup. CF
   unlink does not guarantee physical NAND erasure; physical media protection remains
   required. Nothing is removed under the current task.
8. Qualify real CF timing and Gateway readiness: delays over three seconds must not
   break commit. Beyond 30 seconds Web fails closed; do not forcibly kill a broker
   still saving. After it finishes, classify by reading and resume controllably.
   Test post-commit restart and physical power separately. Export timings, codes,
   generations, signals and aggregates only, never secret files.
