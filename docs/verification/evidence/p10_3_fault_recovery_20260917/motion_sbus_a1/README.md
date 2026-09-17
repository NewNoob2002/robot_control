# F4 A1 — INCOMPLETE: operator used X1 instead of transmitter OFF

User requested continuation of F4 on 2026-09-17. Reuses the accepted F3 A3
3652f15b binary with explicit CLI15, right throttle only, left target zero,
right target <=5rpm, 2950ms cutoff and 3000ms hard limit. No control code changes.
The new target staging directory and independent capture markers are unique.

Preparation: runner-check.json passes live/expired prompts, missing readiness
rejection and three valid/eight invalid SBUS causal cases. artifact-check.json
matches all 68 previously recorded source inputs and the exact executable hash.
Target staging verified machine identity, no competing writers, all five staged
file hashes, --help and the pure control-cycle test; physical marker unconsumed.
No physical success or current powered readiness is implied by these checks.

Run operator_console.py from a visible local terminal. START confirms current
raised/unloaded wheels, unchanged wiring, X1 reset, neutral sticks/CH6 released,
powered and linked transmitter/receiver, drive powered and emergency stop access.
JCAN silent capture precedes target candump and the application. After MOTION_READY,
push forward throttle with steering neutral and immediately switch the handheld
transmitter OFF when right motion begins. Keep the receiver powered. Do not return
neutral before switching OFF; return neutral afterwards. Keep the transmitter OFF
until drive power OFF. Unexpected motion/sound requires immediate X1/power OFF.
No retry or second trial is authorized by this runner.

analyze-sbus.py requires actual positive right feedback before loss, loss before
first zero and automatic cutoff, Source withdrawal, <=100ms loss-to-zero, no later
nonzero/rearm, no X1 interference, bounded >=150ms standstill and exact restoration.
It distinguishes raw frame_lost/failsafe from valid-frame timeout; timeout does not
prove electrical UART silence. Timing is application Reader-batch/deadline to
successful RPDO syscall, not physical RF-switch or independent JCAN latency.
Dual captures must match. Original application protective exit1 remains recorded;
only independently verified SBUS withdrawal can qualify it. Operator actual-action
and OFF confirmation are additionally required. F4 and P10.3 remain OPEN.

## Actual result

A1 is consumed and INCOMPLETE for F4. User explicitly confirmed in chat:
“我看错了，没有关闭遥控，使用了X1，再进行一次复测”. The original terminal
OFF record (including its incorrect transmitter-off assertion) is preserved and
superseded on that point by operator-clarification.json.

All SBUS flags remain0 and Source remains enabled/fault0. X1 appears during
1.524229s of right motion; receive-to-zero12.251us, stable band verified299.691ms
after first zero. This is X1 stopping evidence, not F4 loss evidence.1512 frames
match,801 RPDOs,36 volatile writes/full restoration;4445 trace rows correlate with
capture, no feedback/discontinuity failures, CAN counters unchanged.
Operator confirms drive OFF/right direction+stop normal/left stationary/no sound.
sbus-audit.log retains the rejected F4 oracle; protocol checks only prove bounded
motion/restoration. Both markers consumed. Explicit user retry uses a new A2.
