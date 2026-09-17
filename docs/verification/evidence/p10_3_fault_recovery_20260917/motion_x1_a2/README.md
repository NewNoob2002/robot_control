# F3 A2 — incomplete X1 stimulus; original negative-feedback criterion failed

Single-operator --right-throttle artifact778b0a79 was verified, deployed, and run once
after local START. Throttle alone drove right3..5rpm; left targets/feedback stayed zero.
Operator confirms right direction/stop normal, left stationary, no sound, drive OFF and
X1 locked. Both markers consumed. No automatic retry.

The actual stop was the2.95s automatic cutoff (cause2), not X1. First X1 feedback arrives
260.791ms after first zero on the application clock. This contradicts attributing the
stop to the operator's reported moving-X1 action; the report is preserved unchanged.
Five negative feedbacks follow stop (minimum-1.1rpm); trace feedback_bad1, app exit1.
Original F3 oracle remains failed, and historical motion criteria were not relaxed.

1716 dual-capture frames match. Complete5466-row trace, no overflow;36 volatile writes
and full baseline restoration verified. Stable zero begins310.214ms after first zero
on candump and spans249.730ms; application verifies stop within561.985ms. CAN error/drop
and extended counters unchanged. Timing clocks are recorded separately, not conflated.

inspect-failure.py audits the narrow command/stop/restoration evidence; it does not
accept F3. See failure-analysis.json, metrics-and-counters.json, original-oracle.log
and operator-post-trial.json. Software single-throttle verification is in
../right_throttle_preparation. Another trial needs a new one-shot and fresh readiness;
right<=5rpm/<3s bounds are unchanged. F3 and P10.3 remain OPEN.
