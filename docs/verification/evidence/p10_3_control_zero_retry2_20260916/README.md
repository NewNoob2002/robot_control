# P10.3 zero-only retry2 — 2026-09-16

**PASS zero-target retry2, including operator confirmation; drive OFF; runner consumed.**

Operator authorized a repeat after accidental right-wheel contact. Attempt1 stays
FAILED and consumed. Its incomplete restoration requires power OFF then ON, then
exact baseline readbacks. Operator confirmed 重试已重新上电就绪 before execution.

Same binary SHA256:
f60685c2399fb45a60b288e11e3a3dce82194930b146d42d934679ab7e88c878.
Same robot-dev can0/node1, SBUS wiring,20-second zero-only limits.
Remote: /home/cat/.cache/robot-control/staging/p103-zero-f60685c2-r2-20260916.
Stage log records pretrial staging; the runner has now been consumed.

run-zero.py requires fresh confirmation: 重试已重新上电就绪, power-cycle,
no wheel contact, neutral sticks, released CH6, raised wheels and emergency stop.
operator-confirmation.json records fresh pretrial readiness. JCAN silent capture and candump
precede stimulus; press CH6 only after CONTROL_READY. Final physical observation
and power-off confirmation are recorded in operator-final-confirmation.json. Authorization covers one attempt.

Prior software checks and failed evidence: ../p10_3_control_zero_20260916.
SHA256SUMS excludes itself and Python caches. P10.3 remains OPEN.

## Physical retry2 result

Application and both capture runners exited0. analyze-zero.py passed unchanged:
3149 frames match exactly across target candump and JCAN; all2002 RPDO targets
and all measured speeds are zero.2001 cycles,1557 enabled samples and312 enabled
TPDO feedback frames. All36 volatile writes and NMT order match the oracle;
RPDO/TPDO2 mapping, watchdog and heartbeat baseline restoration verified.
CAN error/drop counters did not increase. TPDO2 intervals49.827–50.316ms.
Observed2 missed periods, maximum lateness29300us, maximum cycle336us;
this is not hard-real-time or production acceptance. First attempt stays FAILED.
Operator explicitly confirms both wheels stayed stationary, no abnormal sound,
and drive power OFF. This bounded zero-target trial is accepted.

The earlier partial reply “确认无异常” is preserved in operator-post-trial.json.
The subsequent explicit final confirmation closes its pending power-off status.
