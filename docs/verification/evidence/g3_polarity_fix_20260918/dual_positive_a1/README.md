# Corrected dual-positive A1 — PASS under approved transient rule

2026-09-18, artifact9b3369368e868d22208c7e2b468d2a3538f0d8303e10d41b937f10980ab1d8b2,
explicit right_sign=-1. New local START followed the corrected-artifact zero A3 PASS.
Operator OFF confirms both wheels vehicle-forward, normal stops, no abnormal sound,
and drive power OFF. The previous5f44146c physical-direction failure is unchanged.

Logical equal positive targets use raw left+1..+5/right=-left. Motion2950.123741ms,
57 simultaneous direction-feedback samples, no restart.1433 globally order/payload
identical capture frames,36 allowed volatile writes/full restoration, unchanged
CAN counters. Stable dual-axis +/-2rpm stop with150ms hold plus fresh frame was
verified357.370762ms after zero (wire356.803840ms).

One right raw+2.5rpm/logical-2.5rpm sample57.701138ms after zero is classified
NORMAL_START_STOP_FEEDBACK_TRANSIENT under the already approved G3 rule. Left at
that sample was+0.8rpm. Raw ranges left-0.3..+5.3/right-6.0..+2.5rpm remain intact;
no active-motion wrong direction, startup violation or hard-bound violation.
The independent wire audit finds the same sample57.128960ms after its zero.

Original application exit1, primary_ok0, feedback_bad1/hard_bad1 and host failure
remain unchanged in raw results and failure-analysis.json. analysis.json records
PASS_USER_APPROVED_START_STOP_TRANSIENT separately. This is acceptance after audit,
not an online-filter change. Three missed periods/max lateness35.139ms remain;
no hard-real-time claim. Runner consumed, no automatic retry/further-mode permit.

Reproduce the separate protocol, signed-target, feedback, timing and operator audit:

    python3 -B docs/verification/evidence/g3_polarity_fix_20260918/dual_positive_a1/audit-transient.py

Scope: raised/unloaded dual-forward and right vehicle-direction correction only.
Dual-negative/turn/differential and their physical protection cases remain pending.
