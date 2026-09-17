# F6 A1 — PASS (2026-09-17)

User request: 推进P10.3F6. One new right-wheel positive <=5rpm/<3s trial,
CLI20, default2950ms proactive cutoff/3000ms hard limit. Transmitter/receiver
remain ON; no external X1/UART/RF stimulus. Fresh local START is mandatory.
Existing fixture robot-dev/machine6923ab3301fb4a8d816759b04ec6bf0a,
can0/node1/500k,UART586D017868,JCAN207F346D5650 is unchanged.

Reuse91d3ce99 artifact:109 compiled project input hashes match the prior locked
cross snapshot. No production/source change. Prior Debug/sanitizer46/46,
scoped static,cross/ELF evidence remains in ../uart_stable_preparation/.
Added motion_throttle_sigterm and stronger source-revocation/no-restart assertions
to the existing integration test; focused motion_sigterm and right-throttle
SIGTERM cases pass in both Debug and ASan/UBSan isolated vcan/PTY fixtures.
Initial debug command omitted the required fixture variable and failed before
CAN output; corrected command passes. Runner check covers direct-child executable,
arguments,parent PID,signal delivery,missing readiness,expired prompts,and seven
invalid causal traces. git diff --check passes. New target stage identity/hash,
--help and pure control-cycle smoke pass; no HIL started during preparation.

Runner observes right feedback>2rpm with left within±2rpm, waits~300ms, verifies
its own unreaped child executable/argv/parent,start ticks and sends one SIGTERM.
Target monotonic before/after timestamps bracket the signal operation (the before
stamp conservatively includes evidence-file write overhead). Independent trace
must show actual motion,signal before first zero and2950ms cutoff,<=100ms upper
bound to zero,source revoked,signal exit2,application protective exit1,internal
shutdown<=100ms,stable stop within1s,exact volatile restoration and bounded exit.
JCAN/candump must match. Pending negative stop-feedback review cannot pass F6.
Local OFF confirmation independently establishes direction/stop/opposite stationary,
no sound,drive OFF and absence of external fault stimulus. No automatic retry.

## Actual result

Local START confirmed powered readiness. Automatic SIGTERM was sent after312.680ms
of observed positive feedback during439.732796ms right-positive motion. Signal
operation start→successful zero-send conservative upper bound312.960us (including
299.252us signal/evidence bracket); internal shutdown9736us. Source revoked,
LifecycleExit::sigterm=2 and cause4 confirmed; no nonzero restart or external fault.
Stable±2rpm feedback verified253.265646ms after zero; application reaped770.229627ms
after signal operation start. Right post-zero raw range−0.4..+4.9rpm includes initial
deceleration; no feedback review item. Expected protective application exit1 and
primary_ok0 are preserved, not relabeled as application exit0.

All1507 candump/JCAN frames match,798 RPDOs,36 exact volatile writes/full baseline
restoration. Trace4408 records/1175 SBUS frames,feedback_bad0,CAN error/drop deltas0.
797 cycles include3 missed periods,max lateness39747us/max cycle132us; no hard-real-
time claim. JCAN verifies byte order and completeness,not independent stop latency.

Operator typed OFF: right direction/stop normal,left stationary,no abnormal sound,
drive OFF,transmitter/receiver ON throughout,no external X1/UART/RF stimulus.
Optional actual-operation text is empty. Both one-shot markers consumed; no retry.
See acceptance.json and audit-result.py for reproducible F6 acceptance.

F6 accepted; the P10.3 planned bounded unloaded HIL matrix is complete as summarized
in P10_3_HIL_CHECKPOINT.md. Prior failed/incomplete trials and F4 A6 pending feedback
review remain historical records. P6 qualification/soak,negative/dual-wheel/loaded
operation,production and delayed-TX kernel work are separate and remain unaccepted.
