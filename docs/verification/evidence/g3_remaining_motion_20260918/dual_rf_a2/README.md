# Dual-wheel transmitter-off A2 — INCOMPLETE (2026-09-20)

The operator explicitly authorized A2 after identifying the unreconnected SBUS
data line in A1. A new local START confirmed reconnection and raised/powered
readiness. The unchanged9b336936/right_sign=-1 artifact was used with <=5rpm,
3s hard motion limit/2950ms cutoff, 60s preparation and +/-2rpm stable feedback.
Only the external supervisor changed: application input_status now reaches the
terminal with waiting_link/neutral_required/release_ch6/arming guidance. Motion
policy and binary are unchanged. Runner bounds,123 source inputs, artifact,
input-status precedence and RF causal audit checks passed offline.

Motion lasted2950.037ms and ended at automatic cutoff (stop cause2). The first
frame_lost flag arrived279.903ms AFTER zero. Target cue to received loss was
3044.534ms; this is not a physical button-duration measurement. The1275 SBUS
frames include1268 flags0 and7 flags4. Therefore this attempt does not prove
RF-loss protection during motion, despite successful normal motion/stop.

Both axes have at least57 simultaneous directional feedback samples; stable
stop verified326.655ms after zero, no restart or feedback violation.1602 frames
match exactly between captures;36 volatile writes/full baseline restoration and
unchanged CAN counters pass. Three missed periods/max lateness36.180ms retained.
Application exit0/primary_ok1 is preserved; the RF-specific runner correctly
remains failed because the expected protective exit1 was not observed.

Operator OFF confirms both vehicle-forward directions/stops normal, no sound,
drive OFF, transmitter OFF, receiver continuously powered and wiring unchanged.
Both one-shot markers are consumed. No automatic retry, extended window or
ground/loaded permission. A1 remains incomplete and its consumed scripts unchanged.

Reproduce:

    python3 -B docs/verification/evidence/g3_remaining_motion_20260918/dual_rf_a2/analyze-incomplete.py

The original analyze-rf.py continues to reject this attempt. A future attempt
needs reviewed manual timing so RF withdrawal occurs during actual motion;
the present <3s bound is not silently extended.
