# F1 zero-X1 A5 — PASS within zero-target scope

Corrected616e8ad3 deployed to the independently identified RK3588 A5 staging directory;
SHA256, help and pure ControlCycle target smoke passed. Local START confirmed current
raised/unloaded fixture, unchanged wiring, emergency stop, neutral CH6/X1 readiness.
Exactly one run; both one-shot markers consumed. Operator entered OFF afterwards:
both wheels always stationary, no abnormal sound, drive power OFF.

Independent analyzer accepts the first X1 recovery through native Disabled:
source/system authorization1→2 only, nonneutral CH6 challenge inhibited, fresh neutral
rearm, ordered Shutdown/Ready/SwitchOn/Enable, followed by1.010s zero-enabled observation.
Live terminal showed ZERO_REENABLED after the first rearm, then STOP/cleanup. The final
complete event is present in application trace; the display suppresses it once cleanup
has begun. No additional CH6 recovery was used to satisfy the oracle.

3406 target/JCAN frames match exactly. All2123 RPDO targets are zero;36 volatile writes
and full original baseline restoration verified. Complete10678-record trace has no
overflow, feedback_bad or discontinuities. Both measured feedback ranges are0rpm.
Configured tolerance remains±1rpm; preflight/cleanup standstill holds161.019/161.009ms.
CAN error/drop deltas zero and extended counters unchanged. Application exit0 in22.850s.

Timing retained:3 missed periods, maximum lateness33.593ms, maximum cycle757us,
shutdown22us. This does not establish hard real-time or loaded-operation acceptance.

Reproduce the independent audit with python3 -B analyze-recovery.py from this directory
(or its full path). See recovery-analysis.json, metrics-and-counters.json,
operator-post-trial.json, and the raw recovery-target/recovery-jcan-once captures.
A4 files match preserved-a4.json. A1/A2/A3 remain failed; A4's combined claim remains
superseded. Moving X1/quick-stop, SBUS loss/failsafe and moving SIGTERM remain pending;
P10.3 is still OPEN. No automatic next physical test is started.
