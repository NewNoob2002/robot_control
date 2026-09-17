# P10.3 left stopping diagnostics and R0 — 2026-09-17

User authorized implementation and staged execution: `完善然后授权执行`.
Software preparation is verified. The original R0 ran once, with complete UART evidence but
an incomplete operator scenario: no return-to-neutral was captured. A/B have not
been staged or started. The last explicit operator power confirmation is OFF;
operator now confirms current sticks neutral and drive OFF. Return timing remains unresolved.

## Implementation and verification

The existing Reader/Source/ControlLoop path now exposes its actual read batch to
an optional diagnostic output. The qualification tool retains bounded raw SBUS,
candidate/request, CAN peek/send and explicit stop-cause records. Storage is
preallocated before device access; export runs only after stop/cleanup. Negative
stop feedback still fails the original criterion. The wire limits remain left
positive <=5rpm, right zero, nonzero <3s. See P10_3_TRACE_SCHEMA.md and the plan.

- Focused trace/motion tests: 2/2, including 13 motion/input scenarios.
- Final HIL Debug: 42/42 (`export-debug-tests.log`, corresponding JUnit XML).
- Final ASan/UBSan: 42/42 (`export-san-tests.log`, corresponding JUnit XML).
- Shared runtime: 37/37 (`runtime-tests.log`, corresponding JUnit XML).
- Scoped static analysis passes; final exporter check in `export-static.log`.
- actionlint passes. Locked cross v3 and ELF audit pass. 59 distinct compiled
  source/header files match the cross snapshot (`v3-source-verification.json`).
- Target identity, staged hashes, --help and pure control-cycle smoke pass
  (`r0/stage.log`). No remote CI result is claimed for this uncommitted tree.

Artifact SHA256:
`9ac673e9b7b039467e346ac6595347c0da5e03cc37a01b8e90b1773df43fa7f2`.
Cross provenance is archived in `v3-cross-*.json.gz`. Earlier v1/v2 outputs are
superseded and were not staged for this trial.

Earlier failed analyzer/static/export runs remain retained. In particular the
prior sanitizer run failed when a nonblocking output pipe filled after cleanup.
The corrected exporter handles EAGAIN with a five-second monotonic deadline;
small-pipe success and deliberately blocked-output failure are exercised. These
fresh passing checks do not rewrite the preserved failed run.

## R0 execution and interpretation

The one-shot target runner consumed `capture-once` and ran UART-only mode for
60 seconds. It opens no CAN lifecycle; the bound send guard denies all sends.
Application exit0, runner elapsed60133.366ms. Raw evidence and result are under
`r0/capture-once/`; independent trace analysis is `r0/analysis.json`.

All25721 records are complete: 8573 SBUS frames, 8574 snapshots, no CAN RX/TX
attempts, no enabled snapshots, CH6 always200 and all frame flags0. One initial
Reader discontinuity and three startup nonhealthy snapshots precede healthy
input; the maximum frame gap is7.954ms.

Input first leaves the calibrated neutral deadband at41.210s. It remains forward/
right through the final frame (CH1=1439, CH3=1534; left candidate5rpm, right0).
There is no completed return-to-neutral within the recording. No negative
candidate is present, but that does not test the planned return/release stimulus.
The trial is **INCOMPLETE_OPERATOR_SCENARIO**, not accepted as the R0 comparison.
It cannot establish or exclude joystick return oscillation as a cause.

No automatic retry or movement followed. Operator reports a return and missing the stop prompt. Its timing relative to
capture remains unresolved; do not assert it occurred after capture. Current
sticks neutral and drive OFF are confirmed in r0/operator-followup.json. Historical left-motion retry2 still fails
its post-stop negative-feedback criterion; no tolerance has been relaxed.

SHA256SUMS excludes itself and Python caches.


## Newly authorized split R0 captures

User subsequently requested: 启动新采集或运动测试. The operator had just
confirmed current neutral sticks and drive OFF. Two independent UART-only
60-second captures used the unchanged verified9ac673e9 artifact, separate
one-shot markers and explicit start/end prompts. No motion test followed.

- r0_retry2_slow:8578 frames, complete trace, flags0, CH6=200, no enabled
  snapshots or CAN RX/TX. Two timed excursions at7.124–17.086s and28.244–32.704s.
  Operator identifies the first as slow return and the second as slow return
  followed by release. Both end neutral; final neutral lasts27.308s. No negative
  candidate or nonzero right candidate occurs. Four initial off-neutral frames
  share the first batch timestamp with neutral; they are preserved but not
  interpreted as a timed operator excursion. CH3 minimum957 is36 below center993,
  still inside the existing normalized input deadband.
- r0_retry2_release:8576 frames, complete trace, flags0, CH6=200, no enabled
  snapshots or CAN RX/TX. The requested natural-release excursion spans
  24.947–29.351s, followed by30.661s neutral. Operator confirms no deliberate backward/left input and current neutral sticks/
  drive OFF (operator-followup.json). Exact mechanical release time was not measured. Six consecutive raw frames AND Source snapshots
  contain right candidates: three -3rpm, then three -4rpm, spanning35.0ms between
  first/last samples. Left candidates remain nonnegative. Initially steering978
  versus throttle375 yields left5/right-3; then steering800/throttle-68 yields
  left3/right-4. Later steering reaches-257 while throttle is in neutral, with
  both candidates zero under the unchanged output deadband. The records establish
  asynchronous return and channel center overshoot, not a transmitted command.

Both runners exited0 with complete evidence; this is not motion acceptance.
The natural-release trace establishes an input/mixer envelope excursion, so the
plan's review gate blocks direct progression to A/B. No deadband, filter, mixer,
wire guard or feedback tolerance was modified. It does not explain historical
LEFT post-stop negative feedback: no negative left candidate was observed here,
and that historical capture had no transmitted negative command.

Exact sequence confirmation is in r0_retry2_slow/operator-sequence.json.
Raw logs/results, analysis and the release waveform are under each new directory.
Both capture markers are consumed. A/B remain unprepared and unstarted. First
release staging was rejected because the slow observer was active (stage.log);
after its exit, stage-after-slow.log verifies successful identity/hash/smoke.
The rejected staging did not create or start a second observer. Original R0
remains incomplete; the new evidence does not rewrite its result.


## A1 preparation after explicit further-motion request

User subsequently requested motion validation. A1 is staged as a held-input,
automatic-cutoff diagnostic with unchanged artifact and guards; see
motion_a/README.md. R0 findings remain valid and no tolerance is relaxed.
Target smoke and JCAN baseline checks pass. Await fresh powered readiness;
no A1 control/capture has started and no readiness has been inferred from OFF.


## A1 completed under fresh powered readiness

A1 reproduced left post-stop negative feedback despite positive input held through
automatic cutoff. Both6355-frame captures match; minimum-1.5rpm across five negative
samples, no negative command. App exits1 as intended; original independent criterion
also fails. Stable zero and exact volatile restoration verified. Operator confirms
normal motion/stop, right stationary, no abnormal sound and drive OFF. A1 consumed;
no retry/B started. Full evidence and interpretation are in motion_a/README.md.

## Right A1 completed — 2026-09-17

Under the user's new right-wheel request and fresh powered readiness, right A1
also reproduced post-stop negative feedback: four samples at100..300ms, minimum
−1.0rpm, no negative command. Both2315-frame captures match; complete7484-row trace
records automatic cutoff. Right3..5rpm lasts2950.273ms, left targets/feedback zero.
Stable zero and exact volatile restoration are verified, but application exit1
and original feedback oracle FAILED remain. JCAN timestamps are all0 and post-trial
CAN counter deltas are unavailable. Operator confirms normal right direction/stop,
left stationary, no abnormal sound and drive OFF. Both right runners consumed;
no retry or next trial. Bilateral recurrence alone does not identify sensor error
or permit ignoring the readings. See motion_right_a1/README.md and its raw evidence.
