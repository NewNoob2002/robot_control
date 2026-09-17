# F4 A3 — INCOMPLETE: automatic cutoff before observed SBUS loss

A1 wrong X1 stimulus and A2 X1-active startup rejection remain INCOMPLETE/consumed.
User explicitly authorized one further retest. Same3652f15b, CLI15, right-only
<=5rpm/<3s; independent stage/markers, no automatic retry. Local START plus typed
X1已复位，关闭遥控器 confirms X1 physically released and the intended stimulus.
Receiver remains powered; transmitter is switched OFF immediately during actual
right motion, then sticks neutral; retain transmitter OFF through drive power OFF.
Full acceptance requires the unchanged independent SBUS causal/protocol oracles
and actual operator confirmation. Offline runner checks pass; no production edits.

## Actual result

The first local confirmation was mistyped and cancelled before readiness/capture;
no marker was consumed. User requested reopening A3; the same unconsumed permit
then launched exactly once after fresh local confirmation.

Right nonzero interval2950.027ms ended by stop_cause2 (automatic cutoff), exit0,
primary_ok1/restore_ok1. All1103 recorded SBUS frames have flags0; Source remains
enabled/healthy through the last control cycle. No X1 bit. The final recorded UART
frame is256.946ms after first zero and still carries throttle1800/flags0. This does
not prove RF loss or UART timeout and does not qualify F4, despite successful exit.

1435 dual frames match,748 RPDOs,36 volatile writes/full baseline restoration;
complete4166-row trace correlates with wire capture, no feedback/discontinuity
failure, CAN counters unchanged. Stable±1.5rpm band verified260.790ms after first
zero, no resumed nonzero. Protocol audit explicitly accepts cutoff/restoration
only; unchanged SBUS oracle rejection is retained in sbus-audit.log.

Operator confirms right direction/stop normal, left stationary, no sound, drive
OFF, receiver continuously powered, no X1 and no return-to-neutral before turning
the transmitter off. Follow-up confirms transmitter power requires a long press.
Recorded motion retains full forward throttle through cutoff; early neutral is
not the recorded stop cause. Actual RF power-off time is not measured, so long
press versus receiver loss-report delay cannot be separated by this capture.

Both markers consumed. F4 remains OPEN. Before another motion trial, coordinate
the actual long-press shutdown against the unchanged short motion window; do not
silently lengthen motion or count automatic cutoff as loss protection. New hardware
observation or retry requires its own authorized scope and current readiness.
