# Right-wheel A1 — executed, post-stop feedback criterion FAILED

Date: 2026-09-17. User explicitly requests right-wheel motion testing and comparison
with the left post-stop negative feedback. This authorizes one right-positive
trial within the existing <=5rpm / nonzero<3s envelope, with left target/feedback
zero. The operator subsequently supplied fresh “右轮A1已上电就绪”; the single trial
executed and both markers are consumed. Final operator confirmation is drive OFF,
right direction/stop normal, left stationary and no abnormal sound. The following
preparation/sequence is historical; it is not a new execution permit.

Reuse unchanged artifact
9ac673e9b7b039467e346ac6595347c0da5e03cc37a01b8e90b1773df43fa7f2.
All59 source/header hashes match the verified v3 snapshot. Existing Debug42/42,
sanitizer42/42, shared runtime37/37 and locked cross/ELF apply to this same binary;
existing virtual motion coverage includes right-wheel operation. No application,
drive parameter, stopping strategy or feedback tolerance is changed.
No current-tree remote CI result is claimed.

New stage:
/home/cat/.cache/robot-control/staging/p103-right-held-9ac673e9-a1-20260917.
Target identity, absence of known writers, uploaded hashes, --help and pure
ControlCycle smoke passed. The right runner uses --single-right, duration60000ms,
90s outer guard,12s cleanup grace and120s/20000frame silent independent capture.
Python syntax and rejection of missing operator confirmation before subprocess
creation passed. JCAN serial207F346D5650 scan and config-get passed; configuration
matches the left A1 baseline. Preparation shell quoting and sandbox SSH/USB failures
are retained in runner-check.json; no physical trial was attempted by those failures.

## Operator sequence and execution gate

Confirm unchanged wiring, both wheels raised/unloaded and untouched, on-site
operator with emergency stop, sticks neutral and CH6 released. Fresh statement:
“右轮A1已上电就绪”. This is current physical readiness, not another scope approval.
Do not reuse the left A1 statement or timestamps.

1. Independent JCAN silent capture starts before target candump and application.
2. CONTROL_READY: neutral sticks; press/release CH6 once, then remain neutral.
3. MOTION_READY: smoothly push throttle FORWARD and steering LEFT together to
   request only right-positive motion; HOLD through automatic cutoff near2.95s.
4. STOP_VERIFIED or confirmed application exit: return sticks to neutral.
   STOP_VERIFIED confirms observed zero, not acceptance of the entire trial.
5. STOP_NOT_VERIFIED, unexpected left motion, motion exceeding3s or abnormal sound:
   emergency stop / power OFF immediately. Never push again or retry automatically.

Collect both captures and complete trace even if application exits nonzero.
Check actual wheel/direction, stop cause, all targets, positive-motion feedback,
post-stop negative samples and their timestamps, stable-zero interval and exact
volatile restoration. Operator direction/stop/noise and final OFF confirmation
are required separately. Both markers are consumed by this one execution.

## Interpretation locked before execution

Original selected feedback envelope remains0..75 tenths rpm; any negative feedback
continues to fail that criterion and is retained. Observe fresh stable zero for
150ms plus the existing extra fresh frame within1s; do not relax this gate.
Record direction, bounded target delivery, stopping and restoration separately,
without declaring overall PASS when the original feedback criterion fails.

Bilateral recurrence would establish a shared observable phenomenon, not prove
Hall/encoder error or exclude real reverse micro-motion. Velocity-estimation error
is the user's hypothesis to investigate. Neither shared recurrence nor normal
visible stopping alone authorizes suppressing feedback or rewriting past failures.
Any later acceptance tolerance needs a separately justified bound and verification.
Compare against left A1 descriptively; different input/peak speeds preclude claiming
a controlled quantitative difference. No automatic B, reverse or dual-wheel trial.

## Executed result — 2026-09-17

The app and target runner exited1; independent JCAN capture exited0. Both captures
match all2315 frames. Complete7484-row trace has no overflow/discontinuity and
records automatic cutoff cause2 plus feedback_bad1. The unchanged original oracle
also fails the selected positive feedback envelope (original-oracle.log).

Right targets+3..+5rpm last2950.273ms; left targets and feedback stay zero, no
negative target is sent, and no nonzero target follows stop. Right feedback during
the motion interval is0..5.5rpm. Input ramps forward/left; final second is constant
CH1=304/CH3=1800 with right candidate5/left0. No pre-stop return-to-neutral occurs.

| Right feedback after zero/Shutdown | Speed |
| --- | --- |
|100.092ms|−1.0rpm|
|200.115ms|−0.2rpm|
|250.128ms|−0.3rpm|
|300.146ms|−0.1rpm|

Final stable zero starts350.148ms after the zero request and spans200.058ms in
candump; application stop verification completes551.972ms after its stop start.
These are separate clock domains, not subtracted from each other. All JCAN frame
timestamps are0, so the independent capture verifies bytes/order only; no second
independent latency measurement is claimed. The wrapper exits on app failure
before can-after.json, so post-trial CAN error/drop counter deltas are unverified.

Exact36 volatile writes, NMT order, final mappings/watchdog/heartbeat, zero targets/
speeds/fault and Disable Voltage readbacks are independently verified restored.
Application restore_ok1 agrees. The audit retains overall FAILED status and rejects
wrong-wheel target tampering, restore-write tampering and an incomplete trace.
See inspect-failure.py, failure-analysis.json, stop-waveform.json, trace-summary.json
and audit-check.json. No firmware or acceptance-limit change was made.

Operator confirms right direction/stop normal, left stationary, no abnormal sound
and drive OFF (operator-post-trial.json). Direction observation, bounded positive
delivery, stable-zero observation and restoration are recorded individually;
the complete motion trial remains FAILED under the original negative-feedback
criterion. No retry or next motion trial started.

Compared with left A1 (five negative samples, minimum−1.5rpm at62..363ms), right A1
also exhibits post-stop negative feedback (four samples, minimum−1.0rpm at100..300ms).
This establishes bilateral recurrence without a negative command or pre-stop stick
return. It does not identify Hall/encoder estimation error, exclude actual small
reverse movement, or justify dropping raw observations. The apparent magnitude
difference is descriptive, not a controlled quantitative improvement. P10.3 remains OPEN.
