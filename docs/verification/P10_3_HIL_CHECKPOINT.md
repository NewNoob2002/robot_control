# P10.3 HIL checkpoint — 2026-09-16

**P10.3 is accepted within the planned bounded, unloaded HIL scope (2026-09-17).**
F6 A1 now passes moving SIGTERM with operator OFF confirmation, completing the
F1–F6 matrix. Earlier sections below preserve historical OPEN/failed checkpoints;
the latest closure section at the end supersedes their current-status wording.
This does not accept production,loaded/reverse/dual-wheel motion,P6 soak or the
unresolved rockchip delayed-TX behavior.

## Completed physical prerequisite

- Operator authorized P10.3 HIL and confirmed unchanged wiring, initially
  powered-off drive, raised wheels, emergency stop and on-site assistance.
  RK3588 machine-id and UART serial586D017868 were checked. No-device smoke and
  10-second SBUS observation passed:1429 frames,1431 snapshots, all invalid/zero
  without an enable edge, final shutdown invalid. Observer has no CAN linkage.
- Added Debug-only/default-OFF qualification operation --runtime-diagnostics,
  fixed two-second observation. It requires exact mode3, both targets/speeds/fault
  zero, non-enabled status, original TPDO1 and unused TPDO2 descriptors/count/timer.
  Unknown baseline fails before writes. No controlword, target, RPDO, mode or
  EEPROM write occurs. The existing RK3588 qualification owner performs correlated
  one-at-a-time SDO transactions, never a JCAN/Python primary sequencer.
- It temporarily enables500ms heartbeat, enters Pre-operational, disables/maps/
  enables TPDO2 as6061:00/8+603F:00/32 with event timer100, enters Operational,
  observes stationary disabled feedback, returns Pre-operational and restores
  the exact baseline. Generation changes forbid restoration into a different
  boot; cleanup failure stays failed and requires operator power-off.
- Artifact SHA256:
  757c232db0a391f75fdf3d84fb67290721bfb8b3d8e00a3d723316639c981f5a.
  A new nonproduction directory and one-shot marker isolate this trial.
  After operator readiness, one driver trial ran at18:35:50+08:00.
  Target candump and silent JCAN207F346D5650 contain **252 identical frames**:
  73 correlated SDO pairs,15 exact volatile writes, and NMT Pre-operational →
  Operational → Pre-operational. There are45 DLC5 TPDO2 frames, all03 00 00 00 00,
  spanning2200.379ms with49.907–50.082ms intervals; the application's two-second
  window counts41. All TPDO1/SDO speeds remain zero and status stays non-enabled.
  TPDO2 descriptors/count/timer and heartbeat0 are restored. Application exits0
  in3872.597ms; CAN error/drop deltas are zero. Operator confirms no motion or
  abnormal sound and final drive power OFF. Software left can0 as found.

## Verification and preserved failures

Host Debug/qualification78/78 and Clang ASan/UBSan78/78 pass without skips;
local ASan uses detect_leaks=0. Final focused4/4, scoped clang-tidy, CI selector,
actionlint, locked-container cross and real-sysroot ELF audit pass. Cross snapshot
matches final compiled sources. Nineteen virtual scenarios cover foreign/active
baseline, missing/wrong/fault diagnostics, nonzero speed, SIGTERM, boot change,
each applied setup write with lost ACK and restore ACK loss. Existing P6 tests
are retained. CI requires the new managed-vcan test. Remote CI run35086866920
passed for commit2489e5df6794f40b5125cf48a42114419f657bf6 on2026-09-16,
including qualification Debug/sanitizer, PDO runtime Debug/sanitizer and host
checks; the unrelated SBUS suite was not selected. This result covers the
committed prerequisite, not the subsequent uncommitted ControlLoop HIL prototype.

The first independent capture window expired during separate approval/dispatch
steps. The target freshness gate rejected launch before invocation: no driver
trial marker or SDO/NMT/RPDO. That preparation remains FAILED/driver-NOT-STARTED.
A new capture continuously orchestrated the still-unconsumed driver trial after
JCAN readiness. There was no retry of a physical SDO operation.

Initial virtual compilation lacked the new method. The first executable test
wrongly expected restored baseline after deliberately losing a restore ACK;
the corrected oracle preserves restore-failed state. Initial static findings
and successful reruns are retained. An analyzer parsing typo was corrected
without repeating hardware. These failures are not rewritten as passes.

## Actual zero-only ControlLoop: retry2 protocol/restoration passed

An isolated Debug/default-OFF ROBOT_CONTROL_BUILD_CONTROL_HIL build combines the
existing bounded qualification bootstrap with one Lifecycle and the actual
ControlLoop/Reader/Source/RuntimeSession. Correlated live layout readbacks qualify
only that owner generation. Bootstrap establishes zero/Ready without enabling;
fresh neutral SBUS authorization owns subsequent zero enable. GNU ld send wrapping
rejects every nonzero SDO/RPDO target, unreviewed controlword and persistent write.
The source retains real calibrated samples; rejected targets are never clamped to
make a trial appear successful. The trial is bounded to20s, with10s setup/cleanup
budgets, temporary1000ms drive watchdog, explicit runtime detachment before SDO
cleanup, zero/Disabled verification and restoration of owned volatile settings.

Partial setup cleanup no longer waits for Operational TPDOs while still in
Pre-operational. Twenty-nine virtual scenarios include all18 applied setup writes
with lost ACK, bad/absent diagnostics, motion contradictions, boot change, SIGTERM,
restore ACK loss and an attached runtime preventing SDO cleanup. A configured
status mask observes low-half X1 bit15 on every runtime event; a pulse followed by
clear cannot revive the old authority. Quick Stop recovery now sends Disable
Voltage only at verified standstill, then Shutdown after Disabled feedback. The
independent virtual peer rejects the former Shutdown shortcut.

Verification: HIL Debug39/39, ASan/UBSan39/39 (local detect_leaks=0), existing runtime
37/37, Release35/35 and P6 qualification78/78 pass without skips. Final scoped
checks6/6 and sanitizer4/4 pass. Five real-executable virtual cases cover zero
enable, nonzero rejection, X1 pulse, SIGTERM and missing diagnostics. The capture
oracle passes virtual replay and rejects target/restoration tampering. Scoped
clang-tidy, actionlint and CI routing pass. Locked cross/ELF audit and comparison
of all77 compiled translation units against the frozen snapshot pass. Target
hash verification, --help and pure ControlCycle smoke pass without CAN/UART test
execution. CI35091917436 for0ee8a44 failed the sanitizer HIL case: a fresh UART
fragment arriving between read and FIONREAD was misclassified as backlog. The
deterministic PTY reproduction fails before the Reader fix and passes afterward.
Reader now drains such arrivals without waiting within the original256-byte
budget; full-budget backlog, service-gap, partial-frame expiry and error/rearm
rules remain enforced. The unchanged physical timing limits pass ten consecutive
sanitizer whole-executable/PTY runs. All five suites above pass again after the
fix. Original CI/reproduction failures are retained, never changed to passes.
The corrected source checkpoint's remote CI is tracked separately.

Staged artifact SHA256:
f60685c2399fb45a60b288e11e3a3dce82194930b146d42d934679ab7e88c878.
Directory: /home/cat/.cache/robot-control/staging/p103-zero-f60685c2-20260916.
The earlier bd98635b artifact was never physically run; its target authorization
is now explicitly retired.
## Physical attempt1 disposition

FAILED/operator-disturbed; one-shot runner consumed. Operator reports accidental
right-wheel contact. All1341 RPDO targets were zero; measured right/high-half
speed0.2 then0.5rpm triggered inhibition.1340 cycles included916 enabled samples.
Cleanup wrote zero targets and Disable Voltage but could not verify restoration
against recent nonzero TPDO feedback. Volatile mappings/watchdog/heartbeat were
unrestored at that failed exit. The subsequent operator-confirmed power-cycle
and retry2 baseline checks resolved this state; see the retry2 result below.
All2138 target frames match the first2138 JCAN frames; eight extra JCAN frames are
passive tail capture. See zero-failure-analysis.json and preserved raw captures.

A separate retry2 ran with the same binary and unchanged limits after explicit
operator power-cycle readiness. Its accepted result is recorded below. Both
runners are consumed; P10.3 remains OPEN.


[Prepared trial, software logs and hashes](evidence/p10_3_control_zero_20260916/README.md).

## Remaining gates

- Physically qualify integrated X1 and quick-stop recovery. Software regressions
  and earlier isolated P6 evidence do not close the new whole-chain HIL gates.
- Then prepare positive single-wheel low-speed bounds and operator-visible phases
  for signs, SBUS loss/failsafe, nonneutral recovery, emergency stop and SIGTERM.
  Negative/simultaneous-wheel motion and CAN loss need their separate scope.
  Consumed capture markers are not permits. Production, loaded operation,
  P6 soak and kernel delayed-TX repair remain unaccepted.

[Raw evidence, scripts and hashes](evidence/p10_3_hil_20260916/README.md).

## Zero-target retry2 execution

After explicit operator power-cycle readiness, the separate one-shot retry2 ran
successfully with the unchanged artifact and limits. Both independent captures
match all3149 frames.2002 RPDOs and measured velocities remain zero;1557 enabled
samples demonstrate fresh SBUS authorization and actual zero-target enable.
All36 expected volatile writes, NMT order and baseline restoration pass the
unchanged analyzer. CAN error/drop deltas are zero.2001 cycles include2 missed
periods; maximum lateness29300us and cycle336us are recorded, not hard-real-time
acceptance. Runner consumed; attempt1 remains failed. The operator explicitly
confirms both wheels stayed stationary, no abnormal sound and drive power OFF.
The bounded zero-target trial is accepted; P10.3 remains OPEN.

[Retry2 raw evidence and analysis](evidence/p10_3_control_zero_retry2_20260916/README.md).

Final operator statement: “本次双轮始终未转、无异常声音，且驱动电源已关闭”.
Current confirmed disposition: drive OFF. No further hardware trial started.

## Prepared single-wheel motion artifact — 2026-09-17

After accepted zero-target retry2 and explicit drive-OFF confirmation, the operator
requested motion testing. A new Debug-only selected-wheel mode preserves the real
SBUS mixer/ControlLoop path and independently constrains final RPDO sends to one
positive wheel <=5rpm for <3s. First zero closes the window; no restart, negative,
simultaneous-wheel, persistent write or nonzero SDO is allowed. Fresh neutral
zero-speed enable is required before motion. Stop requires150ms of fresh zero
TPDO observations within1s and the existing complete restoration.

Debug41/41, sanitizer41/41, scoped HIL/analyzer checks, locked cross build, ELF
audit and target device-free smoke pass. New nine-case motion vcan coverage and
wire-envelope tests are required in CI; remote CI is not claimed for this working
tree. The first left-only one-shot was consumed after explicit powered readiness. It
failed with no zero-speed enable: authorization remained0, enabled_samples0.
No motion-ready phase occurred. All3149 independent capture frames match; all2002
RPDO targets and measured speeds are zero, with no Enable Operation RPDO. Final
volatile baseline restoration is verified. Post-trial physical observation and
power-OFF confirmation are pending; last confirmed drive state ON. No retry.

[Motion preparation, exact hash, bounds and evidence](evidence/p10_3_control_motion_20260917/README.md).

## Left-wheel retry2 preparation — 2026-09-17

Operator reports possible late CH6 action and authorizes one retry. A new artifact
extends only the single-wheel operator window to60s; zero-only maximum20s and
positive selected-wheel5rpm/<3s wire envelope are unchanged. Target guard90s,
cleanup grace12s and silent capture120s/20000frames cover the longer wait.
Debug41/41 and sanitizer41/41 include a CH6-at21s successful virtual trial; seven
CLI bounds, scoped static checks, locked cross/ELF and device-free target smoke
pass. New marker is unconsumed; fresh “左轮重试已上电就绪” confirmation is pending.
Last explicitly confirmed power state remains ON; subsequent OFF was requested
but not confirmed. No retry2 hardware execution has occurred.

[Retry2 bounds, hashes and evidence](evidence/p10_3_control_motion_retry2_20260917/README.md).

## Left-wheel retry2 physical disposition

After fresh “左轮重试已上电就绪”, retry2 ran once. Application and capture runners
exited0 and restoration succeeded, but the independent original motion oracle
FAILED: five negative left-wheel feedback frames after zero/Shutdown, minimum
-3.6rpm. All3409 dual-capture frames match. Left target3..5rpm window2950.260ms;
right target/feedback always zero, no later nonzero command. Stable zero was
verified by the application in695.933ms and the volatile baseline restored.
Cause/physical observation remain unconfirmed. Power OFF requested but not yet
confirmed. Marker consumed; no automatic retry, right-wheel trial or acceptance.

### Retry2 operator follow-up

Operator confirms normal left motion/stop, no abnormal sound and drive power OFF.
The original feedback criterion remains failed. All59 moving-phase feedback frames
are positive0.9..5.5rpm; five negative frames are confined to44..494ms after the
zero/Shutdown request, interleaved with positive/zero feedback. This evidence does
not show a constant sign inversion during forward command. Cause remains open;
no further trial started and no tolerance was relaxed. Current disposition OFF.

## Planned left stopping regression — 2026-09-17

Operator requested planning to distinguish return-to-neutral input variation from
stopping feedback behavior. Current capture ends the positive command at2950.260ms,
consistent with the automatic cutoff, with no transmitted negative target. The
exact operator input and stop-branch cause were not logged. The new plan calls for
bounded in-path SBUS/command/cause diagnostics, offline replay, then separately
reviewed receive-only input, held-input automatic-stop and early-neutral cases.
No tolerance change, new hardware authorization, artifact deployment or execution
is implied. Current operator-confirmed disposition remains drive OFF.

[Left stopping regression plan](../plans/P10_3_LEFT_STOP_REGRESSION_PLAN.md).

### Authorized diagnostics and R0 follow-up

User subsequently authorized implementation/execution. Bounded raw-input,
candidate/request, CAN observation and explicit stop-cause diagnostics are now
implemented. Final Debug42/42, ASan/UBSan42/42, shared runtime37/37, scoped static,
actionlint, locked cross/ELF and device-free target smoke pass. Earlier failed
checks remain archived. Artifact9ac673e9 uses bounded post-cleanup trace export;
the motion envelope and negative-feedback criterion have not been relaxed.

One drive-OFF UART-only R0 completed with8573 frames, complete trace, no CAN
RX/TX attempts and no authorization. It captured forward/right input from41.210s
through the60s window end, but no return-to-neutral. Therefore the planned slow/
natural-release comparison is INCOMPLETE_OPERATOR_SCENARIO, despite application
exit0. Operator reports a return whose timing remains unresolved, and now
explicitly confirms current sticks neutral and drive OFF. See the R0
operator-followup.json evidence.
R0 marker consumed; A/B not staged or started. No automatic retry.

[Diagnostics and R0 evidence](evidence/p10_3_left_stop_regression_20260917/README.md).


### Split R0 follow-up under new execution authorization

Two separate drive-OFF UART-only60s captures completed with the unchanged9ac673e9
artifact. Slow/mixed-return capture8578 frames: no negative or right-nonzero
candidate; operator identifies slow return then slow return followed by release.
Requested natural-release capture8576 frames: six consecutive right-negative
candidates (-3 then-4rpm), with unequal axis return and channel center overshoot.
The same candidates appear in actual Source snapshots; selected/approved output
and authorization remain zero, with no CAN RX/TX. Both traces complete and finish
neutral. Operator confirms no deliberate backward/left input in the second capture,
and current sticks neutral / drive OFF. Exact mechanical release time is unmeasured.
This triggers the planned input-envelope review gate: no motion A/B started.
It does not establish the cause of historical left post-stop negative feedback.
Both new runners consumed; original incomplete R0 and prior motion failures retained.


### A1 held-input motion diagnostic prepared

After the R0 finding, user explicitly requested further motion validation.
A1 is separately prepared with unchanged verified9ac673e9, existing left<=5rpm/
nonzero<3s/right-zero guards, and held input through automatic cutoff. Return
stimulus is excluded during motion; an envelope excursion still fails. New target
stage identity/hash/device-free smoke and JCAN read-only baseline checks pass.
Fresh powered readiness is pending; current operator-confirmed state remains OFF.
No control/capture started; A1 markers unconsumed. See the regression evidence
motion_a/README.md for the exact one-shot scope and sequence.


### A1 executed; post-stop negative feedback reproduced

After fresh A powered readiness, both captures match6355 frames; complete21178-row
trace records stop cause2. Left3..5rpm, nonzero2950.300ms, right targets/feedback0,
no negative target. Input remains forward/right through automatic cutoff (ramped,
not perfectly constant). Five left-negative feedbacks occur62..363ms after stop,
minimum-1.5rpm. Application correctly exits1 with feedback_bad1 and no overflow;
original independent oracle fails too. Exact36 volatile writes and final baseline
readbacks are verified restored; stable zero starts412.630ms and spans200.042ms.
Operator confirms normal left direction/stop, right stationary, no abnormal sound,
and drive OFF. A1 markers consumed; no retry/B started. Active return-to-neutral
is not necessary for this phenomenon; underlying drive/mechanical cause unresolved.
See motion_a/failure-analysis.json and operator-post-trial.json in regression evidence.

### Right-wheel A1 comparison prepared — 2026-09-17

User requests a right-wheel motion trial to compare post-stop feedback with left
A1. The unchanged9ac673e9 artifact supports --single-right; all59 source/header
hashes match the previously verified snapshot. A new stage passes target identity,
hash, --help and pure-cycle smoke. Right-specific wrapper syntax and missing-ready
rejection pass; JCAN read-only identity/configuration checks pass. Existing right
vcan coverage applies to this unchanged artifact. No feedback tolerance is relaxed.

Scope: one right-positive<=5rpm/<3s trial, left targets/feedback zero, hold forward/
left input through automatic cutoff; retain complete trace and dual capture.
Both markers remain unconsumed; fresh right powered readiness is pending. Last
operator-confirmed state remains OFF; no capture/control or physical trial started.
Shared negative feedback, if reproduced, would not alone establish sensor error or
justify ignoring it. P10.3 and the original left failures remain OPEN/FAILED.

[Right A1 scope and preparation](evidence/p10_3_left_stop_regression_20260917/motion_right_a1/README.md).

### Right-wheel A1 executed — bilateral post-stop negative feedback

Fresh “右轮A1已上电就绪” preceded the single trial. Both captures match2315 frames;
7484-row complete trace has feedback_bad1 and automatic cutoff cause2. Right
targets3..5rpm last2950.273ms; left targets/feedback remain zero, no negative target
or post-stop nonzero target is sent. Final-second input is constantCH1=304/CH3=1800,
with no pre-stop neutral return. Four right-negative samples occur100..300ms after
zero/Shutdown:−1.0,−0.2,−0.3,−0.1rpm. App exit1 and original oracle FAILED are retained.

Stable zero starts350.148ms and spans200.058ms in candump; application verifies stop
in551.972ms on its own clock. Exact36 volatile writes and final baseline readbacks
are verified restored. JCAN timestamps are all0: bytes/order match, independent
JCAN latency is unavailable. App-failure wrapper skips the can-after snapshot, so
post-trial CAN counter deltas are not claimed. Offline audit rejects wrong-wheel,
restoration and incomplete-trace mutations; no application/tolerance change.

Operator confirms right direction/stop normal, left stationary, no abnormal sound
and drive OFF. Both markers consumed; no retry or further trial. Both wheels now
have positive-direction and stable-stop observations, but full motion acceptance
still fails the original post-stop feedback criterion. Bilateral recurrence does
not alone establish Hall/encoder error or justify ignoring feedback. P10.3 OPEN.

### Whole-chain fault/recovery preparation — 2026-09-17

User authorizes X1/quick-stop, SBUS loss/failsafe, nonneutral recovery and moving
SIGTERM while preserving prior evidence;144 old stop-regression evidence hashes
are unchanged. New zero-X1/zero-SBUS modes observe actual withdrawal, nonneutral
challenge, neutral hold and fresh zero reenable; independent wire gate stays zero.
Recovery preparation requires current605A:00=5 before writes, never changes it.
Production safety/arbitration and the negative-feedback criterion are unchanged.

Debug43/43, ASan/UBSan43/43, scoped static/CI, locked cross/ELF and122 compiled
source/project-header comparisons pass. Eight recovery virtual cases and moving
X1/failsafe/silence plus existing movingSIGTERM pass. Original failed observer
checks are archived; no remote CI claim. Artifact2f97bdba is staged for F1 zero-X1
with target identity/hash/device-free smoke and JCAN read-only baseline verified.
Fresh F1 fixture/powered readiness is pending; both markers unconsumed, no capture
or control started. Latest confirmed state OFF. F2 and moving physical trials are
pending; actual UART silence needs a specific physical stimulus. P10.3 OPEN.

[Fault/recovery plan](../plans/P10_3_FAULT_RECOVERY_HIL.md) and
[software evidence / F1 preparation](evidence/p10_3_fault_recovery_20260917/README.md).

### F1 zero-X1 A1 executed — incomplete stimulus sequence

F1 A1 ran once after fresh readiness and remains FAILED / INCOMPLETE, with both
markers consumed. Operator reports only CH6; no X1 operation. Only fault_ready
was reached, about5.160s before the60s control deadline; no fault_observed or
recovery phases occurred. Application exited1 with control_hil_recovery_incomplete.
The late phase prompt was received together with exit and was not forwarded as
an instruction. This trial does not establish an X1 recovery defect or acceptance.

Offline audit matches8912 frames across both captures,6002 zero RPDOs, zero speed
feedback, inactive X1, healthy mode/fault, complete29485-row trace, and exact36
volatile writes/restoration.605A:00=5 preceded all writes. CAN errors/drops and
extended error counters did not increase. Normal recovery oracle remains FAILED;
--incomplete verifies only this incomplete trial's protocol/restoration.
Operator separately confirms both wheels stationary, no abnormal sound and drive
OFF. No retry or F2/moving trial started. Review phase coordination and obtain
fresh readiness before any new one-shot; P10.3 remains OPEN.

### F1 A2 direct operator console prepared — 2026-09-17

User authorized improved phase coordination and another X1 recovery verification.
A new A2 one-shot uses the unchanged2f97bdba binary and60s zero-only window.
A local operator terminal displays live phase actions/countdown; explicit START
confirms current fixture/power readiness before capture/control. Exit, cleanup,
expired-window and stale-progress paths suppress instructions or abort. Offline
prompt regression, target identity/hash/device-free smoke and read-only JCAN
baseline checks pass. Consumed A1 files are preserved. A2 has no acceptance until
actual execution, independent audit and operator disposition are recorded.
See [A2 preparation](evidence/p10_3_fault_recovery_20260917/zero_x1_a2/README.md).

### F1 A2 executed — zero-target feedback failure

A2 was executed once after local terminal START. Live CONTROL_READY→FAULT_READY
handoff took3.895s, leaving about56s; the application stopped710.003ms after
fault_ready, before any X1 stimulus. Thus this is not the A1 manual-window timeout.
Only neutral CH1/CH3 and CH6 authorization occurred. All targets stayed zero;
left TPDO feedback was0.3/0.2/0.4rpm, right zero, X1 inactive, mode3/fault0.
The strict trace observer latched feedback_bad and stop cause6. Application exit1.

Target913 frames match the first913 of JCAN923; the10 extra independent tail
frames contain only zero-speed/healthy feedback and heartbeat. Complete2442-row
trace matches transmitted frames. First bad feedback→zero Shutdown6 was4.276ms;
SDO zero targets and Disable Voltage read back correctly. Cleanup then read
606C:01=0xffffffff (-1 signed, documented0.1rpm units), aborted before restoring
RPDO/TPDO2 maps, watchdog and heartbeat; only21 of36 expected writes occurred.
Stable zero appears149.978ms after the first bad frame and lasts900.157ms in the
remaining target capture, but this does not retroactively prove restoration.
CAN error/drop counters did not increase. Normal oracle remains FAILED; the
failure auditor rejects three target/trace/capture mutations. No retry occurred.

Operator confirms drive OFF, wheels visually stationary, no abnormal sound and
no X1 operation. Empty terminal post-trial input is retained separately from the
explicit chat follow-up. User requests a new zero-target feedback tolerance;
old criteria/results stay unchanged. Sensor-versus-physical origin is not proven.
Any future run must revalidate the full current baseline because cleanup failed.

### A3 prospective tolerance prepared — user-selected ±1rpm

User selected±1rpm for zero-target qualification and confirmed both wheels
visually stationary/no abnormal sound/OFF after A2. New0d379ed8 artifact explicitly
selects10 tenths rpm; default remains0. Signed trace/observer/runtime/SDO cleanup
checks agree; preflight/cleanup hold>=150ms, transmitted targets remain zero.
Debug43/43, sanitizer43/43, scoped static, locked cross/ELF and122 compiled-source
hashes pass. A1/A2 remain failed; A2 baseline restoration is incomplete. New A3
one-shot awaits local terminal START and full live baseline readback before writes.
See [A3](evidence/p10_3_fault_recovery_20260917/zero_x1_a3/README.md).

### A3 actual disposition — baseline mismatch

A3 executed once after local START, failed before CONTROL_READY: 2000:00 reads1000,
required0. Exactly8 SDO uploads, zero writes/NMT/RPDO,16-row complete trace. The
standstill tolerance was not reached. restore_ok1 means no new ownership to clean,
not recovery of A2 residual settings. Operator confirms no CH6/X1, wheels stationary,
no sound and OFF. Both markers consumed. Known-residual restoration is required
before another full baseline preflight; no automatic retry or acceptance claim.

### Verified residual restoration — 2026-09-17

Restoration executed once with fresh local START and passed. Target343 frames
match JCAN435 at offset92; exact18 zero/Disable Voltage/map/timer writes, only NMT
Pre-operational, no RPDO/enable. Complete332-row trace; standstill holds159.951,
160.009 and162.003ms. All original baseline objects read back, including watchdog0,
heartbeat0, RPDO descriptor60600008 and empty TPDO2 mapping/timer. CAN errors/drops
unchanged. Operator local OFF confirms stationary wheels/no sound/OFF. Both markers
consumed. This closes residual restoration only, not X1/quick-stop acceptance.

### A4 causal review — 2026-09-17

A4 ran once, all targets zero, full baseline restored; operator confirmed stationary
wheels/no sound/OFF. After the first rearm the prompt stalled, so the operator made
another CH6 off/on. The first X1 recovery (source generation2) and later CH6 quick-stop
recovery (generation3) each have ordered feedback/commands and a one-second enabled
zero hold. The old observer incorrectly required QuickStopActive for native X1 and
borrowed the later event. Original PASS is retained but its combined causal claim is
superseded; the corrected oracle rejects that combined claim. See zero_x1_a4's
observed-sequences-analysis.json and README. P10.3 remains OPEN; corrected observer
hardware validation and remaining integrated fault/motion cases are pending.

### A5 corrected observer physical verification — 2026-09-17

A5 PASS within zero-target X1 scope with corrected616e8ad3 artifact: source/system
1→2 only; native-X1 Disabled path, nonneutral rearm rejection, first neutral rearm
and1.010s enabled hold independently verified. ZERO_REENABLED appeared on the local
terminal; application completion is recorded before cleanup.3406 dual-capture frames
match,2123 RPDOs all zero,36 volatile writes and exact baseline restoration verified.
Both feedback ranges0rpm with configured±1rpm tolerance;161.019/161.009ms standstill
holds. Trace10678 records complete, CAN errors/drop deltas0. Operator local OFF confirms
stationary wheels/no abnormal sound/power OFF. Both markers consumed. Timing retained:
3 missed periods and33.593ms maximum lateness; no hard-real-time claim. See
zero_x1_a5/README.md and recovery-analysis.json. Remaining moving/integrated fault
cases remain pending; P10.3 stays OPEN. A4 original evidence unchanged.

### F2 A1 — zero-target SBUS timeout recovery accepted (2026-09-17)

Unchanged616e8ad3 completed one F2 trial. Actual fault:10.660s valid-frame gap and
100.020ms timeout withdrawal; all6648 received valid frames have flags0.51 raw bytes
occur inside the gap, so electrical UART silence/F5 is not claimed. Nonneutral rejection,
neutral fresh1→2 rearm and complete quick-stop/Disable Voltage/new Disabled/enable order
pass the independent oracle.8586 matching frames,5721 zero RPDOs,36 writes/full baseline
restoration, complete26675-record trace. Operator local OFF confirms stationary/no sound.
Markers consumed; flags4/12 failsafe and moving scenarios remain pending; P10.3 OPEN.
See zero_sbus_a1/README.md and recovery-analysis.json in fault-recovery evidence.

### F2 A2 accepted; A1 stimulus corrected — 2026-09-17

User confirms A1 switched receiver power while transmitter stayed ON: prior A1 timeout
recovery evidence remains valid, but transmitter-loss attribution is superseded.
Original A1 files remain unchanged; correction is archived in zero_sbus_a2.
A2 switches only the handheld transmitter; operator confirms receiver continuously powered.
Valid-frame flags0→4→12→0, max gap10.920499ms; actual frame_lost then failsafe recorded.
Independent oracle verifies nonneutral rejection and fresh neutral quick-stop recovery.
4010 matching dual-capture frames,2542 zero RPDOs,36 writes/full restoration, app exit0.
Operator OFF confirms stationary/no abnormal sound. Both markers consumed. F2 zero-target
transmitter-loss recovery accepted; no isolated failsafe-only, moving or F5 claim.
See zero_sbus_a2/README.md and recovery-analysis.json. P10.3 remains OPEN.

### F3 A1/A2 and single-operator mode — 2026-09-17

A1 never moved: initial throttle produced bilateral3rpm candidates rejected before send;
1882 matching frames,1121 zero RPDOs, full restoration. User requested single-person input.
New Debug-only --right-throttle projects only left command to zero before arbitration,
retaining Source/raw/authority; steering must stay neutral, reverse input rejects.
Debug/sanitizer43/43, static/cross/ELF and target smoke pass; artifact778b0a79.
A2 actual right3..5rpm motion worked, but cause2 automatic cutoff preceded X1 by260.791ms
on the application clock. Five post-stop negative feedbacks (min-1.1rpm) retain FAILED.
1716 matching frames, full36-write restoration, stable zero verified561.985ms; operator
OFF/no sound/left stationary confirmed. Both A1/A2 markers consumed. F3 remains INCOMPLETE,
P10.3 OPEN. See motion_x1_a1, motion_x1_a2 and right_throttle_preparation in fault evidence.

### 单轮运动验收收口：CLOSED；反馈判据修订为 ±1.5rpm

用户确认停止时负反馈来自驱动器内部速度估算，按正常波动记录，并明确关闭该保留项。
左右轮方向、实际停止、对侧静止及无异响已有现场确认；两轮 A1 原始轨迹按±1.5rpm复核通过，
见 standstill_15_revision/single-wheel-revised-audit.json。旧抓包及原判据失败保留，新增修订结论；
历史−3.6rpm样本不属于新阈值数值范围，不将其重写为±1.5rpm内。
后续 HIL 统一使用 --zero-feedback-tenths-rpm 15（控制模式默认15），包含运动/停车与清理。
原始速度不改写，非零目标、单轮限速/3秒上限不变，明显反向超阈值仍拒绝；>=150ms稳定观察保留。
F3运动中X1时序仍须独立通过，不能由单轮收口或容差替代。当前新F3 A3准备重跑。

### F3 A3 — moving X1 PASS under revised ±1.5rpm criterion

User-authorized rerun uses3652f15b --right-throttle --zero-feedback-tenths-rpm15.
Debug/sanitizer43/43, static/cross/ELF and target smoke passed. One actual right-wheel
run: X1 observed at1.848s before cutoff; application receive→zero-send32.084us,
no later nonzero command; stable feedback within±1.5rpm verified299.740ms after first zero.
Post-stop raw feedback−0.5..+0.2rpm retained/classified normal in approved band.1433 dual
frames match,746 RPDOs,36 writes/full restoration, complete4178-record trace with no
feedback failure. Exit1 is expected X1 revocation, verified independently. Operator
OFF/right direction+stop normal/left stationary/no sound/X1 locked confirmed. A3 markers
consumed; no next test started. F3 accepted; F4/F5/F6 remain pending, P10.3 OPEN.
See motion_x1_a3/README.md in fault-recovery evidence. Single-wheel acceptance CLOSED.

### F4 A1/A2/A3 — consumed; transmitter-loss motion remains INCOMPLETE

All three use unchanged3652f15b/right-throttle/CLI15 and separate one-shot stages.
Offline runner causal/prompt checks and each target device-free smoke pass.
A1: operator confirms mistaken X1 actuation, transmitter stayed ON.1512 matching
frames/full restoration; flags0/source_fault0. Original terminal transmitter-OFF
assertion is superseded by explicit chat clarification; raw records unchanged.
A2: operator forgot X1 reset; startup rejects control_hil_x1_active. Two matching
frames, one SDO6041 read, no writes/NMT/RPDO, cycles0; no configuration change.
A3: initial mistyped local confirmation cancelled before marker consumption;
reopened on user request, then invoked exactly once. Actual right motion ends at
2950.027ms by automatic cutoff/code2, exit0. All1103 SBUS frames flags0, Source
enabled through final cycle, no X1; no observed SBUS loss.1435 matching frames,
748 RPDOs,36 writes/full restoration,4166-row complete/correlated trace. Stable
band verified260.790ms after first zero; counters unchanged. Operator OFF/right
direction+stop normal/left stationary/no sound/receiver powered/no X1 confirmed.
Transmitter requires a long press; raw throttle stays1498..1800 during motion,
final1800, so early neutral is not the recorded stop cause. Actual RF-off time
and its relation to receiver loss-report delay remain unmeasured. Original F4
oracle failures and all consumed markers are preserved. No automatic retry or
expanded duration. See motion_sbus_a1/a2/a3 in fault-recovery evidence.
F4/F5/F6 remain pending; P10.3 OPEN.


## Latest F4 A5 disposition and ±2rpm revision (2026-09-17)

A4 exited before motion because transmitter-OFF failsafe candidates were mistaken
for right-throttle commands. Startup handling now waits within the original60s
budget and reports waiting_link/neutral_required/release_ch6/ready/arming; valid
neutral input and a fresh CH6 edge remain required. Once enabled, loss terminates
the trial without automatic rearm. Explicit8s motion hard limit/7950ms cutoff
was approved; the default motion window remains3s.

A5 (38800cfe, CLI15) observed actual flags0→4 during7089.866087ms right motion,
receive→zero22.167us, no restart/X1,2313 matching capture frames and full restoration.
It originally FAILED because one−1.6rpm stop sample exceeded±1.5rpm. That failure,
feedback_bad=1 and original raw evidence remain unchanged. User explicitly raised
the startup/stop band to±2rpm. Independent retrospective A5 analysis passes under
that criterion, including275.795564ms stable-band verification. F4 is accepted
under the revised criterion; F5/F6 remain pending, P10.3 OPEN. Operator confirmed
OFF, right direction/stop normal, left stationary, no abnormal sound. A1..A5
runners remain consumed; no new physical run occurred for this revision.

Control HIL now defaults to20 tenths rpm (accepted0..20); library defaults stay0.
Debug45/45, sanitizer45/45, scoped static, locked cross and ELF checks pass.
New b8f9192f artifact has not been deployed or run on target. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/standstill_20_revision/README.md.


## Latest F4 A7 — PASS with deferred stop-feedback review (2026-09-17)

New cd8bd1c5 artifact records selected-wheel negative stop-band excursions for
post-trial review after successful zero RPDO, bounded by±7.5rpm hard limit and
opposite-wheel±2rpm. Active motion/startup/cleanup guards and1s/150ms stable stop
checks remain enforced. Pending does not mean accepted. Debug/sanitizer45/45,
scoped static/cross/ELF/source-snapshot and target device-free smoke passed.
A6 hit7950ms cutoff; SBUS loss187.906ms later. User confirmed delayed shutdown.
A6−3.5/−3.8rpm review items remain pending; original incomplete trial preserved.
Fresh user-authorized A7 observed frame_lost during3.700s motion, receive→zero
49.584us, stable band verified280.680ms, no restart/X1/feedback review items.
1807 dual frames match,1006 RPDOs,36 writes/full restoration,CAN counters unchanged.
Operator OFF/right direction+stop normal/left stationary/no sound/transmitter OFF/
receiver continuously powered confirmed; optional action description is empty,
physical button timing unmeasured. Expected protective exit1 accepted independently.
Four missed periods/max lateness39.757ms retained; no hard-real-time claim.
A7 PASS,F4 accepted; F5/F6 remain pending,P10.3 OPEN. All A1..A7 runners consumed.
See docs/verification/evidence/p10_3_fault_recovery_20260917/motion_sbus_a7/README.md.


## Latest F5 UART A1 — INCOMPLETE (2026-09-17)

User authorized F5 by disconnecting only the SBUS signal port; fresh local START
confirmed USB/power/ground retained and transmitter ON. cd8bd1c5/CLI20 zero-only
runner used once. Signal interruption left42 raw bytes/one complete frame in the
last batch; after59.970ms the Reader partial_timeout3 produced discontinuity and
session2→3. Source revoked immediately (fault9); the recovery observer, which
accepts RF flags/valid-frame timeout only, exited before100ms timeout/recovery.
2276 matching frames,1339 all-zero RPDOs,36 writes/full baseline restoration,
no feedback violation or CAN counter changes. Operator OFF/stationary/no sound
confirmed. Short action response “是的” does not separately establish signal
reconnection. Both runners consumed, no retry. F5 remains OPEN; a future F5
observer must handle expected partial-frame interruption while retaining immediate
revocation and rejecting unrelated service-gap/backlog/transport errors. Preserve
this original incomplete run. F4 accepted; F5/F6 keep P10.3 OPEN. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/zero_uart_a1/README.md.


## Latest F5 A2 and extended-window preparation (2026-09-17)

13ccbd9d dedicated UART observer A2 handled the expected partial timeout and
completed1009.991ms hold, then exited in phase2 on source_fault2: two rejected
reconnect candidates1789.982ms after release. Not total-window exhaustion.
1694 matching frames,935 zero RPDOs,36 writes/full baseline restoration,one
expected discontinuity,no feedback failure/CAN counter changes. Operator OFF,
stationary wheels/no sound confirmed; action free text empty. A2 remains INCOMPLETE
and consumed; no automatic rerun. See zero_uart_a2/analysis.json.

User requests longer intervals/window. New bcd4cfe9 preparation supports F5-only
120s total,5s minimum hold,45s reconnect/challenge. It records initial rejected
candidates while authority stays revoked; INPUT_RESTORED marks healthy Disabled
input before nonneutralCH6. Hard failures and fresh rearm remain enforced.
Debug/sanitizer46/46,scoped static,cross/ELF/109-input snapshot checks pass. New
artifact not deployed or run on target; A3 scripts prepared authorized=false.
F5/F6 remain OPEN; F4 accepted. Fresh A3 authorization/readiness is required.
See docs/verification/evidence/p10_3_fault_recovery_20260917/uart_window_preparation/README.md.


## Latest F5 A3 — raw silence verified, recovery INCOMPLETE (2026-09-17)

User explicitly authorized A3; bcd4cfe9/CLI20 with120s total,5s hold,45s reconnect
used once after target identity/hash/smoke and fresh local START. Reader raw-byte
gap16.350s and5.010s hold verified; expected partial timeout revoked immediately.
After healthy Disabled input/INPUT_RESTORED, two rejected candidates470.023ms
later caused phase2/source_fault2 protected exit. Recorded axes remained neutral
and authority revoked. Not window expiration; physical framing-defect cause unknown.
3756 matching capture frames,2367 all-zero RPDOs,36 writes/full restoration,
no feedback failure/CAN counter change. Operator OFF/stationary/no sound confirmed;
action free text empty. Both markers consumed; no retry. Raw silence portion
verified,full nonneutral/fresh-rearm F5 recovery remains OPEN; F6 pending. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/zero_uart_a3/README.md.


## Latest F5 A4 — PASS with continuous stable recovery (2026-09-17)

User-authorized91d3ce99/CLI20,120s total/5s hold/45s reconnect. Phase2 repeated
parser rejections retain revoked authority and reset stable/challenge progress;
>=1s received healthy neutral Disabled input precedes each live recovery cue.
Debug/sanitizer46/46,scoped static,cross/ELF/109-input snapshot,target smoke pass.
A4 raw-byte silence10.430s and5.010s hold verified. One rejected reconnect candidate
retained/resynchronized; stable1s then nonneutral rejection,neutral,newCH6 and
quick-stop/Disable Voltage/fresh Disabled recovery all independently verified.
4722 matching frames,3039 zero RPDOs,36 writes/full restoration,no feedback failure
or CAN counter changes. Operator OFF/stationary/no sound/signal-only/USB-power-ground
unchanged/transmitter ON confirmed; optional action text empty. Both markers consumed.
F5 accepted within Reader raw-byte silence/zero-output recovery,not measured voltage
silence or100ms-trigger evidence (initial withdrawal was partial timeout). A1/A2/A3
remain incomplete. F6 remains pending; P10.3 OPEN. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/zero_uart_a4/README.md.


## F6 A1 PASS and bounded P10.3 closure — 2026-09-17

User requested推进P10.3F6. Reused91d3ce99/CLI20 with unchanged109 compiled input
hashes and prior cross/ELF/static evidence. New runner identity/causality guards and
right-throttle SIGTERM integration assertions pass; both focused scenarios pass
Debug and ASan/UBSan. Fresh target identity/hash/no-device smoke and local START
preceded one3s-hard-limit/right<=5rpm invocation. No production source changed.

Signal sent after312.680ms observed positive feedback during439.733ms motion.
Signal-operation→zero-send upper bound312.960us,internal shutdown9.736ms,stable
±2rpm stop253.266ms,process reap770.230ms; Source revoked and no restart. Expected
protective exit1/cause4/signal_exit2 independently accepted.1507 dual-capture frames
match,798 RPDOs,36 volatile writes/full baseline restoration,4408 trace records,
no feedback review/CAN counter changes. Three missed periods/max lateness39.747ms
remain recorded; no hard-real-time claim. Operator OFF/right direction+stop normal/
left stationary/no sound/transmitter+receiver ON/no external fault confirmed.
Both markers consumed. See [F6 A1](evidence/p10_3_fault_recovery_20260917/motion_sigterm_a1/README.md)
and its acceptance.json; raw exit/error records are unchanged.

| Planned acceptance | Accepted evidence |
| --- | --- |
| SBUS receive-only and TPDO2 layout prerequisite | Earlier prerequisite above; p10_3_hil_20260916 |
| Actual zero ControlLoop start/stop | p10_3_control_zero_retry2_20260916 |
| Independent single-wheel direction and stop | standstill_15_revision; user-approved interpretation preserved |
| F1 X1 zero recovery/nonneutral rejection/fresh rearm | zero_x1_a5 |
| F2 SBUS timeout and RF flags recovery | zero_sbus_a1 (receiver interruption),zero_sbus_a2 (transmitter loss) |
| F3 moving X1 | motion_x1_a3 |
| F4 moving transmitter loss | motion_sbus_a7 |
| F5 UART interruption/recovery | zero_uart_a4; Reader raw-byte silence,initial partial-timeout withdrawal |
| F6 moving SIGTERM | motion_sigterm_a1 |

Trial names above are under evidence/p10_3_fault_recovery_20260917 unless otherwise
specified. All planned bounded P10.3 gates are accepted. F5 does not prove measured
voltage silence or a100ms initial withdrawal; F4 A6's−3.5/−3.8rpm historical review
items remain pending and are not replaced by A7/F6 passes. No historical failed or
incomplete attempt is rewritten. Negative/simultaneous-wheel or loaded operation,
CAN-loss expansion,P6 remaining qualification,full-chain long soak and kernel
repair retain their separate boundaries. No further hardware permit is created.
