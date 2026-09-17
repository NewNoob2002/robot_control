# F2 A2 — PASS zero-target frame_lost/failsafe recovery

One handheld-transmitter OFF/ON run with unchanged616e8ad3; local operator confirms
receiver continuously powered. Final OFF confirms stationary wheels/no abnormal sound.
Both one-shot markers consumed; no subsequent trial started.

Actual valid-frame flags0→4→12→0:2771 healthy,71 frame_lost,786 frame_lost+failsafe frames.
Maximum valid-frame gap10.920499ms; no UART-timeout/silence scenario is inferred.
Independent oracle verifies withdrawal, nonneutral CH6 rejection, neutral hold, fresh
rearm and QuickStopActive→Disable Voltage→new Disabled→Shutdown/SwitchOn/Enable.
Fault begins with frame_lost; subsequent failsafe is observed within the same withdrawn
interval, not a separate isolated failsafe-only injection.

4010 target/JCAN frames match;2542 RPDO targets all zero;36 volatile writes and full
baseline restoration verified. Complete trace, no feedback_bad/discontinuity; see metrics.
Standstill holds162.022/160.920ms; configured tolerance±1rpm. Application exits0 in27.108s.
CAN error/drop deltas0; extended counters unchanged. Timing:3 missed periods, maximum
lateness35.904ms, cycle931us, shutdown26us; no hard-real-time claim.

A1 operation correction: user confirms receiver power OFF/ON while handheld transmitter
remained ON. Its timeout recovery evidence is retained, but it is not the intended
transmitter-loss trial. See prior-operation-correction.json. Original A1 files remain
byte-identical to preserved-a1.json; no historical logs or criteria were rewritten.

Reproduce with python3 -B analyze-recovery.py. Raw captures, protocol analysis and local
post-trial confirmation are retained here. F2 transmitter-loss recovery is now accepted
within this zero-target scenario; moving X1/SBUS/SIGTERM and F5 remain pending.
