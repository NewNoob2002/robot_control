# G3 dual-positive A1 — FAILED physical direction (2026-09-18)

Fresh local START authorized both raised/unloaded wheels at equal positive
1..5rpm targets,<3s, steering neutral/throttle forward only. Artifact5f44146c
and123 compiled inputs unchanged. No deliberate fault stimulus or retry.

## Result

The operator reports vehicle-forward left motion but vehicle-backward right
motion, explicitly referenced to the vehicle viewed from behind. Therefore this
trial is FAILED_PHYSICAL_DIRECTION even though the software/protocol audit passes.
This is actual direction mismatch, not normal brief startup/stop feedback noise.
The user subsequently confirmed drive OFF, both wheels stopped and no abnormal
sound. Original local free text and later clarification are retained separately
in operator-off.json and operator-clarification.json.

Independent protocol audit:1200 globally order/payload-identical capture frames;
36 allowed volatile writes/full original baseline restoration, exact zero and
Disable Voltage. Both raw targets equal positive values,57 simultaneous fresh
positive-feedback samples on each axis. Nonzero2950.023547ms; stable dual-axis
stop verified301.699661ms after zero, within1s with150ms plus new frame.
Raw left feedback-0.8..+5.3rpm/right-1.2..+5.5rpm; no feedback violation/review.
CAN counters unchanged. Application exit0/primary_ok=1/restore_ok=1 cannot grant
physical direction acceptance.583 cycles,440 enabled samples,3 missed periods,
max lateness36.840ms,internal shutdown8us retained; no hard-real-time claim.

## Offline binding diagnosis

Current tools/control_hil/main.cpp initializes RuntimeConfig with left_sign=+1,
right_sign=+1, left target/feedback low half. domain/drive/runtime.cpp already
applies each sign in both target encoding and feedback normalization. With the
operator's vehicle-frame observation, the present right+1 binding is inconsistent
with vehicle-forward. A corrected software binding is expected to require
right_sign=-1 while retaining left_sign=+1; this is not yet implemented or tested.

A one-line runtime sign change alone is insufficient: zero_gate.cpp passes raw
wire targets into MotionGate, trace.hpp checks raw feedback signs, and the current
G3 offline auditor also assumes logical signs equal wire signs. Those boundaries
must consistently distinguish logical wheel direction from raw drive polarity,
retain raw evidence, and be tested before a new artifact or physical trial.
No drive persistent direction parameter, runtime source, binary or interface was
changed in this investigation.

Historical single-wheel signed trials and the user's transient-feedback acceptance
are preserved. Their generic direction confirmations do not establish the new
explicit vehicle-coordinate right-forward binding. Remaining G3 vehicle-direction
trials need that binding resolved; no later motion step is automatically started.
All host/target/capture one-shot markers are consumed.

## Replay

    python3 -B docs/verification/evidence/g3_raised_motion_20260918/dual_positive_a1/audit-direction.py

This verifies protocol/feedback/stop/restoration and emits
FAILED_PHYSICAL_DIRECTION from the explicit operator evidence. The original
analyze-motion.py correctly rejects the non-OFF/non-normal local disposition;
it must not be weakened into a PASS. Raw physical-once/, target-result.tar.gz,
jcan/session.jsonl and all operator statements are preserved.
