# Phase 6 closure baseline

Review date: 2026-09-15. **OPEN — CHECKPOINT, NOT FINAL PHASE ACCEPTANCE**.

Use [PHASE6_CHECKPOINT.md](PHASE6_CHECKPOINT.md) for the accepted bounded trials
and their limits, [the evidence index](evidence/README.md) for original records,
and [the review/CI record](P6_CHECKPOINT_REVIEW_20260915.md) for final-source checks.

## Remaining closure requirements

- Soak is deferred by the operator until SBUS/full-chain integration. V3 failed
  after 309 s / five cycles; V4 never started and its authorization is retired.
  Both isolated CANopen lifecycle and full-chain endurance remain required.
- Resolve JCAN USB receive framing before a new independent capture, then
  regenerate the exact artifact/preflight and obtain new bounded authorization.
- New inhibitor protocol validation is software-tested; historical HIL and
  cross results apply only to their recorded artifacts. Current cross/target
  limitations are explicitly recorded with this checkpoint's CI.
- Non-destructive fault-reset/electrical bus-off stimuli are unavailable.
  Mechanical brake output is not applicable to this fixture (September 14
  operator confirmation). Composite X1 stop/reset behavior is accepted within
  its two recorded trials; this is not general system safety certification.
- Exact rockchip_canfd retry-worker/stop concurrency and matching full kernel
  commit remain unresolved. The accepted userspace inhibitor is a mitigation.
- Complete outstanding evidence and final-source acceptance before declaring
  Phase 6 or the integrated system complete. Subsequent component development
  is a scheduling exception, not authorization for motion or production use.

The accepted TPDO feedback trial retains small operator-accepted speed
excursions. Vibration remains a hypothesis. Later passes never erase failed
NMT, cable-loss, power/X1 wrapper or soak attempts.

## Historical provenance

The earlier closure build was based on review commit
e8c6c16e3dd4e16ec70f6b8f11de694966736b02 in a separate local checkout, with parent
1d1314d25269d38f9679cd2c15e23df65afcd2a4. Its source archive hash was
1c5c25967de3c4d8aa89c75f95bb28f9cac81f84b28889c2706f810e5ab2f09b.
Those 48-test/clean-cross results apply to that older source, not the later
manual-TPDO artifact, whose 53-test evidence is linked from the checkpoint.

The full pre-checkpoint narrative and logs are preserved in
[evidence/archives/p6_7-history.tar.gz](evidence/archives/p6_7-history.tar.gz),
including member p6_7_20260910_closure/BASELINE_BEFORE_CHECKPOINT.md.
Use the [evidence index](evidence/README.md) for all direct and archived trial paths.
No new physical operation is authorized by this baseline.
