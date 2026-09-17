# F3 A3 — PASS moving X1 within revised±1.5rpm feedback criterion

User requested unified±1.5rpm threshold and a rerun. New3652f15b was deployed to an
independent one-shot stage after software verification. --right-throttle uses one
forward throttle stick, steering neutral, other hand operating X1. Local START and
post-trial OFF plus actual-operation confirmation are recorded. Both markers consumed.

Right motion lasted1847.938ms on the application clock. X1 was received during that
motion, before automatic cutoff; first successful zero RPDO syscall followed32.084us
later. No subsequent nonzero target. This measures application receive-to-send only,
not physical switch response or independent JCAN latency. Fresh multi-frame standstill
within±1.5rpm verified299.740ms after first zero; application stop observer reports297.962ms
from its own later stop start. X1 remained locked. Operator confirms right direction/
stop normal, left stationary, no abnormal sound and drive power OFF.

1433 target/JCAN frames match exactly,746 RPDOs,36 volatile writes/full baseline restore.
Complete4178-row trace, no overflow/feedback_bad/discontinuity; trace frames correlate
with physical capture. CAN errors/drops/extended counters unchanged. Left feedback0;
right moving peak5.6rpm, post-stop estimate-0.5..+0.2rpm. Raw negatives retained and
classified normal within the newly approved band; targets remain right<=5rpm,left0.

Application exit1/primary_ok0 is retained: control_hil_motion_envelope healthy0/armed0
at X1 withdrawal, stop cause5. It is not a feedback failure. Independent analyze-x1.py
checks causal order, full signed feedback, bounded stop and restoration separately from
exit code. F3 is accepted under this explicit physical-fault criterion. No blanket
acceptance of exit1, old late-X1 trials or unrelated failures is implied.

Timing:3 missed periods, maximum lateness35.953ms, cycle277us, shutdown20us; no hard-real-
time claim. See protocol-analysis.json, x1-analysis.json, metrics-and-counters.json,
operator-post-trial.json and unchanged raw captures. F4/F5/F6 remain separate pending
scenarios; P10.3 still OPEN. No automatic next hardware trial is started.
