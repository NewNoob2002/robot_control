# Phase 6 closure baseline

Review date: 2026-09-10. Status: **OPEN — VERIFIED CHECKPOINT, NOT FINAL PHASE ACCEPTANCE**.

The current requirement table, operator-accepted TPDO result and software test
counts are consolidated in [PHASE6_CHECKPOINT.md](PHASE6_CHECKPOINT.md). Use that
record instead of the superseded chronological summaries.

## Remaining closure requirements

- Measure watchdog response using the reviewed passive TPDO stream and qualify
  recovery without assuming a latched inhibit. Preserve trial 2's confirmed
  stop/restart and trial 4's corrected initial-motion evidence.
- Qualify physical heartbeat/TPDO loss, moving SIGTERM and controlled interface
  loss/reopen as separate bounded stimuli after the checkpoint's remote CI passes.
- Record applicability decisions for emergency inputs, brake outputs, gated fault
  reset and electrical bus-off; do not silently mark untested requirements PASS.
- Complete final-source regression and artifact/isolation review after remaining
  implementation. This checkpoint's software pass is not final HIL acceptance.

The manual TPDO speed test is **PASS by operator acceptance**. Small left-speed
excursions are accepted for that test; chassis vibration remains a hypothesis.
The three-packet/24-byte kernel RX discrepancy is tracked separately and does not
negate the accepted functional TPDO result. Neither observation establishes
production feedback eligibility or safe automatic motion reauthorization.

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
