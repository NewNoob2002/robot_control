# Phase 6 verified checkpoint — 2026-09-11

**Phase 6 remains in progress.** The synchronous feedback repair and separate
left/right RPDO trials pass. Revised synchronous stop/loss physical tests have
not started: two execution requests timed out in automatic approval before
process creation. That is an execution-review blocker, not an HIL failure.

## Current disposition

| Item | Status | Evidence and limits |
| --- | --- | --- |
| P6.1–P6.4 contract, protocol, executor and zero-target lifecycle | PASS | Accepted historical inventories and bounded trials |
| Independent first-motion and earlier stop trials | HISTORICAL PASS | Exact earlier artifacts; not a pass for revised synchronous loss tests |
| Synchronous packed SDO left +5 rpm / 3 s | PASS | Nonzero TPDO feedback; operator-confirmed selected-wheel motion and normal stop |
| Single-RPDO left and right +5 rpm / 3 s | PASS | Each wheel tested separately; normal stop, other wheel stationary, no abnormal sound; original mapping restored |
| Latest software qualification | PASS | Host 60/60, ASan/UBSan 60/60, cross/ELF and target isolated-vcan |
| Revised synchronous stop/loss physical requalification | NOT EXECUTED | Two automatic approval timeouts before process creation |
| Cable/power loss, moving SIGTERM and applicable fault/soak tests | OPEN | No new acceptance claimed |
| Simultaneous nonzero wheels, negative RPDO motion, loaded operation | NOT QUALIFIED | Outside the three successful physical repair trials |
| P6.7 final phase acceptance | OPEN | Remaining physical gates and final regression required |

The [repair record](P6_SYNC_PACKED_PDO_REPAIR.md) explains the fixed mappings,
cleanup behavior, per-artifact evidence and hardware limits. The
[evidence index](evidence/README.md) locates original captures, operator
observations, failure history and archives.

## Implemented changes and validation

- [Safety/I/O corrections](P6_REVIEW_SAFETY_IO_FIXES.md): restricted standalone
  activation, live TPDO mapping preflight, receive-boundary handling and logging
  that preserves shared sink flags.
- [Diagnostic corrections](P6_REVIEW_DIAGNOSTICS.md): silent local CANopen
  startup/reopen and correctly deferred deadline events.
- [Online attachment](P6_REVIEW_ONLINE_STARTUP.md): current heartbeat/SDO
  evidence replaces a requirement for historical boot-up or operator power cycling.
- [Feedback diagnosis](P6_TPDO_FEEDBACK_DIAGNOSIS.md): asynchronous enabled
  motion updated independent speed but not the packed object; the synchronous
  packed-target route restored SDO and TPDO speed feedback on this fixture.
- First-motion and stop/loss qualification retain verified mode 1. The optional
  fixed RPDO path checks, temporarily changes and restores its exact baseline.
  Startup requires zero; stopping permits bounded deceleration and still
  verifies zero. Watchdog observation is passive; its first subsequent TX clears
  both targets instead of querying SDO while a motion target may be retained.

Latest staged remainder artifact SHA256:
fa1944fdba2dd98cbe8ebbeebc34174baafe1eba0d930b33ae306fbd47091205.
The [manifest](evidence/p6_sync_remainder_20260911/manifest.json) preserves exact
file/source hashes. Host and sanitizer each pass 60 tests. Clean pinned GCC11.4
cross build, ELF audit and target isolated-vcan pass. Default Debug/Release
builds remain qualification-OFF. Static checks report zero errors and eight
existing advisories. GitHub Actions performs host/static, Debug and sanitizer
qualification with mandatory vcan; it does not run RK3588 HIL. The runs attached
to the pushed commit are the authoritative remote-CI result.

## Historical evidence and archive

The [accepted manual TPDO trial](evidence/p6_6_20260910_manual_tpdo_trial_1/RESULT.md)
remains accepted, including the operator-reported minor other-wheel speed
excursions. Vibration is a proposed cause, not an established safety fact.
The [review HIL record](P6_REVIEW_HIL.md) preserves aborted preparations and
superseded artifact status. Later passes do not erase earlier failures.

Original raw CAN evidence, pre/post configuration observations, operator
confirmations and one-shot markers remain directly readable. Software logs are
compressed with per-file hashes in the
[archive manifest](evidence/archives/MANIFEST.json); checksum validation resolves
both direct and archived records. Earlier milestone detail remains in its dated
verification documents and Git history.

## Next work

1. Finish this archive/submission checkpoint and monitor its remote CI.
2. Resume the same prepared watchdog execution request after execution review
   permits it; the [blocker record](evidence/p6_sync_remainder_20260911/execution_blocker.json)
   confirms no physical attempt occurred. Do not repeat successful repair trials.
3. Complete separately bounded stop/loss requalification and remaining physical
   applicability/fault/soak decisions before closing Phase 6.

Production motion services, full SBUS/M4, ROS2, permanent drive parameters,
loaded operation and production deployment remain outside this checkpoint.
