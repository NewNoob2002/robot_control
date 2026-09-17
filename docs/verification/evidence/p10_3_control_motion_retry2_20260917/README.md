# P10.3 left-wheel retry2 — 2026-09-17

**Retry2 FAILED_post_stop_negative_feedback; runner consumed. Operator now confirms drive OFF; P10.3 OPEN.**

Operator reported pressing CH6 but suspected the prior capture window had ended,
and explicitly authorized one new capture attempt. Attempt1 remains FAILED:
authorization0, enabled_samples0, no motion, all targets/feedback zero and baseline
restoration verified. Exact operator timing is unknown. The prior runner remains
consumed. Last explicit power state was ON; the requested subsequent OFF has not
been confirmed. Do not infer readiness from prior statements.

## Change and bounds

Single-wheel CLI operator window now allows up to60000ms (previously20000ms).
Zero-only CLI still rejects values above20000ms. The motor wire gate and its
header are byte-identical to attempt1: selected left positive1..5rpm only, right
zero, one nonzero window <3s, first zero/rejection closes it permanently. Neutral
fresh CH6 authorization,20 zero-enable samples, feedback envelope, physical-stop
observation and restoration are unchanged. No clipping of the opposite wheel.

This retry uses60000ms. Target guard90s plus12s cleanup grace; orchestration110s;
independent silent capture120s/20000frames, target candump20000frames. The longer
capture limits cover waiting; they do not extend nonzero command duration.
No automatic retry, negative motion, simultaneous-wheel motion, fault reset,
persistent writes, interface changes, CAN-loss stimulus or loaded use.

## Verification and artifact

Debug41/41 and ASan/UBSan41/41 pass with no skips. The motion executable suite now
has10 scenarios, including fresh CH6 at21s (beyond the former window), successful
motion/stop/restoration and independent capture-oracle replay. Seven CLI boundary
checks verify unchanged zero-only limits and single-wheel60000/60001 boundaries;
valid cases use a nonexistent UART and fail before CAN access. The pre-change
binary rejected the new60000ms request. Scoped clang-tidy passes.

Locked Docker / actual target sysroot cross build and ELF audit pass.55 distinct
compiled source files match the archived snapshot; unchanged wire gate confirmed.
Target identity, final file hashes, --help and pure control-cycle smoke pass.
No remote CI result is claimed for this current working tree.

Artifact SHA256: 00fe79835bc6134461352d98b6279a8f808967b3882322d3477052de7c2e77ff.
Target directory: /home/cat/.cache/robot-control/staging/p103-left-00fe7983-r2-20260917.
Cross metadata and exact source attestation are archived as gzip. stage-smoke.log
records the unconsumed marker and absence of active writers at staging time.

## Execution gate and operator sequence

run-motion.py requires a fresh reply “左轮重试已上电就绪” (<300s), confirming
current drive power ON, untouched raised wheels, neutral sticks, CH6 released and
available emergency stop. No confirmation/capture has been fabricated or started.
Silent JCAN starts first, then target candump, then the actual RK3588 application.
At CONTROL_READY press CH6 once and release, staying neutral. At MOTION_READY
smoothly push throttle forward and steering right together, then return neutral.
Left only <=5rpm; right nonzero or reverse input fails before the syscall. One
nonzero burst, at most3s; operator wait60s is not a60s motion permit.

Operator must confirm selected wheel/direction, opposite wheel stationary,
normal stop/no abnormal sound and drive OFF afterward. Failure requires OFF,
inspection and a new authorization/artifact before another attempt. Existing
kernel delayed-TX and unmeasured electrical limitations remain unchanged.
SHA256SUMS excludes itself and Python caches.

## Physical retry2 outcome

Fresh operator readiness received and both captures started before stimulus.
Application/capture runners exited0, with primary_ok1/restore_ok1. Independent
analysis nevertheless FAILS the original nonnegative selected-feedback criterion:
five negative left-wheel TPDOs occur after the zero/Shutdown request, minimum
-36 tenths rpm. The feedback cause is not established and no tolerance was relaxed.
All3409 capture frames match exactly. Left commands are positive3..5rpm for
2950.260ms; right commands and feedback stay zero. No nonzero command follows the
stop request. The application verified stable zero within695.933ms; all36 writes
and final volatile baseline restoration are verified. See motion-failure-analysis.json.
User was asked to power OFF; physical wheel/sign/stop observations and final OFF
remain pending. No further motion or automatic retry occurred. This trial is not
accepted merely because the application exited0. First attempt stays failed.

## Operator follow-up and sign timing

Operator confirms normal left-wheel direction/stop, no abnormal sound and drive
power OFF; exact statement is retained in operator-post-trial.json. This does not
change the failed predeclared feedback criterion. During positive command all59
left feedback frames are positive (0.9..5.5rpm). Five negative samples occur only
after the zero/Shutdown RPDO, at44.074,94.076,194.052,444.150,494.160ms. Positive
and zero samples interleave; this is not a constant forward-command sign inversion.
Full stop waveform is in stop-feedback-detail.json. Cause remains unestablished;
no further hardware trial or threshold change was made.
