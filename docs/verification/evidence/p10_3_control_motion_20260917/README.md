# P10.3 selected-wheel motion preparation — 2026-09-17

**LEFT attempt1 FAILED_no_zero_enable; runner consumed. Last confirmed drive state ON; post-trial power-OFF pending. P10.3 OPEN.**

User requested “进行运动测试” after accepting zero-target retry2. First trial is
left only; right needs its own one-shot artifact/readiness record after left
postflight. Prior failed/consumed trials stay preserved. No automatic retry.

## Artifact and scope

Artifact SHA256: 3c2d6a0fb35eac571e4a732731258e480e35925a9013ada6eca4d2a8dbd8d1f6.
Stage: /home/cat/.cache/robot-control/staging/p103-left-3c2d6a0f-20260917.
Source checkpoint is the current dirty working tree based on4c7390f; exact locked
cross-source snapshot/metadata are archived as gzip.55 distinct compiled source
files plus the new motion-gate header match that snapshot. No remote CI success
is claimed for these uncommitted changes. The existing user .clang-tidy was not
changed or added; scoped static analysis uses explicit configuration.

The real calibrated Reader/Source/ControlLoop/RuntimeSession remains the command
path. No replacement command generator or clipping of the unselected wheel.
Zero mode retains its zero-only wire check. Selected-wheel mode uses an explicit
owner-thread borrowed gate at GNU send: zero before arming; after20 neutral
zero-speed enabled cycles, accept selected positive1..5rpm only; opposite zero.
Reject negative/both-wheel/overspeed targets before syscall. First nonzero starts
an immutable3s window; first subsequent zero or rejection closes it permanently.
The main loop stops proactively at2950ms; the syscall gate rejects at3000ms.
SDO never permits nonzero velocity, Enable Operation or persistent writes.

Sampled selected feedback must remain0..75 tenths rpm (a trial rejection bound,
not a manufacturer guarantee); opposite feedback must stay exactly zero. Capture
analysis checks every frame, not only control-cycle snapshots. At least two fresh
positive feedback samples are required. Stop reads live CAN observations because
runtime state is terminal after stop; require150ms of fresh zero TPDO observations
within1s, then existing zero-SDO/Disable Voltage/PreOp restoration. A failed stop
send proceeds directly to cleanup; any unverified result requires operator OFF.
No hard-real-time, loaded-use or rockchip delayed-TX repair claim is made.

## Verification

- Debug41/41 and ASan/UBSan41/41, no skips. Local ASan detect_leaks=0.
- Final changed-scope HIL5/5 passes, including both analyzer replay paths and
  target/restoration tampering rejection. Final sanitizer motion suite passes.
- Nine virtual motion cases: left, right,3s expiry, both-wheel rejection, reverse
  rejection, missing motion feedback, wrong-wheel feedback, stop timeout, SIGTERM.
- Original five zero-only executable cases and29 bootstrap cases remain passing.
- Gate unit checks cover exact3000ms boundary, first-zero closure, no restart,
  missing arm, clock regression, controlword, wheel and speed limits. Actual
  wrapper tests check borrowed binding, wire decoding and default-zero fallback.
- Scoped clang-tidy and actionlint pass; CI builds/requires both new tests.
  Default Release/P6 suites were not rerun: their code/configuration is unchanged.
- Locked Docker / actual target sysroot cross build and ELF audit pass. Target
  identity, hashes, --help and pure control-cycle smoke pass without CAN/UART.
- Initial virtual stop failure remains recorded in initial-failure.md. Broad
  static output is retained; final scope excludes unrelated existing headers and
  the project's pragma-once convention. The CI selector has no --help entry;
  its classify function was checked directly after that invocation failed.

## First left-wheel operator sequence and gates

Keep the drive OFF until ready to perform this concrete trial. Keep both wheels
raised, unloaded and untouched; emergency stop/power removal immediately available.
Keep sticks neutral and CH6 released. Reply “左轮运动测试已上电就绪” only after
power ON and those physical conditions are current. No readiness file is inferred.

run-motion.py starts silent JCAN before candump. At CONTROL_READY press CH6 once
and release while neutral. At MOTION_READY smoothly move throttle forward and
steering right together, approximately half travel, then return both to neutral.
The actual mixer is left=throttle+steering, right=throttle-steering; an unintended
nonzero right command fails the trial, never gets masked. One nonzero burst only,
up to3s. The observed wheel/direction must be confirmed from the application phase
and capture, not chat timing. Final physical stop/no-abnormal-sound/power-OFF
confirmation is required. Fault/loss/recovery and right-wheel trials remain open.

## Evidence and recovery

authorization.json and safety-preflight.json bind the single attempt to this hash,
node1/can0 and unchanged calibrated UART wiring. Electrical amplitude/RX tolerance
remain unmeasured; no electrical changes are made. motion-once.py consumes its own
remote physical-once marker. run-motion.py requires a fresh statement (<300s).
Outer guard45s plus12s cleanup; silent capture70s/10000frames. Capture failure
requests abort. Scripts/markers from zero trials are never reused for execution.

stage-smoke.log is the initial staging record; stage-prompt-verification.json
records the final runner hash after correcting the operator prompt text only.
This attempt now has consumed physical/readiness/capture records; do not rerun it.
SHA256SUMS excludes itself and Python caches.

## Physical left attempt1 result

Operator confirmed 左轮运动测试已上电就绪. Independent silent JCAN and target
candump ran before the application. Application exited1 after21131.102ms:
control_hil_no_zero_enable,2001 cycles,0 enabled samples, authorization0 throughout.
No MOTION_READY occurred. Both captures match all3149 frames; all2002 RPDOs have
zero targets, no Enable Operation RPDO, all observed speeds zero. All36 volatile
writes completed, final mappings/watchdog/heartbeat and zero/Disabled state were
restored. Stop and restoration software results are successful. No motion pass
is claimed. Operator button action/timing is not yet established. Power OFF was
requested; current post-trial OFF and physical observations remain unconfirmed.
No automatic retry occurred. See motion-failure-analysis.json and raw captures.

Operator follow-up reports pressing CH6 but suspects it was outside the window;
exact timing remains unverified. One new retry was authorized and separately
prepared in ../p10_3_control_motion_retry2_20260917 with a60s operator window.
This consumed failed attempt is not reused. Post-trial OFF remains unconfirmed.
