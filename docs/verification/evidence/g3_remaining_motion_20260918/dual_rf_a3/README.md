# Dual-wheel transmitter-off A3 — accepted (2026-09-20)

The user explicitly requested another attempt without changing the program or
3s window and would shut the transmitter down sooner. New one-shot A3 runners
differ from A2 only in attempt identifiers. Unchanged9b336936/right_sign=-1,
<=5rpm,3s hard motion/2950ms cutoff,60s preparation and +/-2rpm stable band.
Target identity/hash,123 source inputs, runner bounds/cues and RF causal
audit checks passed; fresh local START preceded capture and target execution.

Dual-forward motion lasted2489.997ms. Actual frame_lost (flags0→4/source fault3)
arrived during motion before cutoff. Reader-receive-to-successful-zero-send
was19.827us on the application monotonic clock; physical button/RF transmission
timing was not measured. No UART discontinuity, X1 or restart.48 simultaneous
directional feedback samples; stable stop verified367.524ms after zero,
including >=150ms continuous +/-2rpm and a fresh frame.

1446 dual-capture frames match exactly.36 bounded volatile writes/full baseline
restoration and unchanged CAN counters pass.1126 SBUS frames/4265 trace records
retained. Three missed periods/max lateness32.952ms: no hard-real-time claim.

Original analyze-rf.py fails on feedback_bad; feedback_hard_bad1, primary_ok0
and application exit1 remain unchanged. One right raw+3.4rpm/vehicle-logical
-3.4rpm sample occurred67.908ms after zero (left raw-1.0rpm at that sample).
This is classified NORMAL_START_STOP_FEEDBACK_TRANSIENT under the previously
approved criterion. All raw feedback remains within +/-7.5rpm; pre-enable,
active-motion direction, exact target/no-restart and bounded stable-stop checks
pass on application and wire clocks. This is a retrospective single-sample
audit; no online guard, tolerance, movement limit or raw record was modified.

Operator OFF confirms both vehicle-forward directions/stops normal, no sound,
drive OFF, transmitter OFF, receiver continuously powered and wiring unchanged.
Both one-shot markers consumed; A1 no-input and A2 late-loss remain incomplete.
Scope: raised dual-forward transmitter-loss stop only, no recovery/rearm,
ground/loaded acceptance or full G3 closure.

Reproduce original failure with analyze-rf.py; reproduce independent acceptance:

    python3 -B docs/verification/evidence/g3_remaining_motion_20260918/dual_rf_a3/audit-transient.py

Original failure is retained in original-audit-failure.txt. Supplemental audit
reuses the prior UART transient check with the RF-specific causality and
operator checks, without replacing the consumed trial scripts.
