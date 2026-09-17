# F4 A5 — original ±1.5rpm verdict FAILED; SBUS withdrawal verified

New38800cfe fixes invalid/disabled startup SBUS being treated as a reverse command.
Explicit --motion-window-ms8000, right-only<=5rpm, left0, CLI15. Same60s total
window includes startup wait and never resets. New stage/capture markers; A4 remains
INCOMPLETE/consumed, not retried in place. See ../startup_wait_preparation.

Fresh local START confirms current raised/unloaded drive readiness and X1 reset;
receiver must remain powered but transmitter may initially be OFF. INPUT_WAIT asks
to switch it ON and wait for healthy SBUS. INPUT_NEUTRAL requests centered sticks;
INPUT_RELEASE requests CH6 release; CONTROL_READY permits one new CH6 edge.
ARMING holds neutral until MOTION_READY. Then forward throttle only; after1s of
positive feedback POWER_OFF_READY asks for long-press transmitter OFF. Keep throttle
until transmitter OFF then neutral; never press CH6 again, keep receiver powered.
Stop/exit suppress all further phase actions. Unexpected behavior uses X1/power OFF.

Independent oracle must see new loss after actual motion begins, not reuse initial
flags12. Require loss before first zero/7950ms cutoff, <=100ms loss-to-zero, no resumed
output, bounded stable standstill, dual capture agreement and full restoration.
Software success and operator actual-action/OFF confirmations remain separate gates.

## Original result under CLI15

Waiting/ready/release/arming and POWER_OFF_READY prompts executed correctly.
Operator reports transmitter initially ON, prompts normal, long-press shutdown at
the cue, no premature neutral, receiver powered, no X1. Drive OFF/right direction
and stop normal/left stationary/no abnormal sound confirmed in local terminal.

Actual frame_lost flags0→4 occurs during7089.866ms right motion, before7950ms cutoff;
Reader batch receive to successful zero RPDO syscall22.167us; no later nonzero.
2313 dual frames match,1356 RPDOs,36 exact volatile writes/full baseline restoration;
complete7172-row trace, no discontinuity, CAN counters unchanged. The feedback_bad
latch is real: one post-stop right feedback sample−1.6rpm at75.696ms exceeds±1.5rpm.
The band becomes stable and is independently verified325.837ms after first zero.
Original F4 verdict is FAILED_FEEDBACK_LIMIT, not PASS; audit-result.py isolates the
valid SBUS withdrawal from that failure. sbus-audit.log retains the original oracle
rejection. Both markers consumed; no new trial. Later explicit user±2rpm revision
belongs in ../standstill_20_revision and must not rewrite this original evidence.
