# Phase 6 verified checkpoint

## Current update — 2026-09-18

The three-hour disabled CANopen lifecycle soak is now accepted under the
user-approved +/-2rpm measured standstill band.175 cycles/10807.299s and
348246 exact dual-capture frames pass; no counter increases or JCAN length
error/disconnect/reconnect occurred. Nine right-feedback samples at-0.3..+0.3rpm
are accepted; the original strict-zero audit and raw data remain unchanged.
Operator confirms both wheels stationary, no abnormal sound and drive power OFF.
See [the accepted trial](evidence/rk3588_can_soak_20260918/README.md).

Future physical tests use +/-2rpm for near-zero measured feedback, never for
targets, safety command payloads, fault bits or protocol fields. This test does
not establish a permanent USB/kernel repair or integrated SBUS/ControlLoop,
moving/loaded or production soak acceptance. Those remaining boundaries keep
P6 OPEN. Earlier failed/retired trials remain unchanged.

The delivered source node d6bbaa9c88185c451567b6672528dbc95e5aed15 passed
GitHub Actions35312438999. Its +/-2rpm policy passes local Debug/sanitizer78/78
each, locked cross/ELF and focused checks; the new artifact is not deployed.
See [policy evidence](evidence/standstill_policy_20260918/README.md).

Next work is continuous diagnostics and a bounded full-chain zero-target entry,
then a one-hour real-SBUS full-chain soak, raised negative/dual-wheel qualification and a
separate ground-readiness gate. Per the September18 user decision, subsequent
stage soaks are one hour; a five-hour system stability test follows completion
of all main features. The accepted three-hour evidence is unchanged.
The current HIL trace is bounded and exported
only after stop; it is not a long-running monitor. See
[the implementation and acceptance plan](../plans/POST_SOAK_GROUND_READINESS.md).

## Historical checkpoint — 2026-09-15

**Historical disposition: Phase6 OPEN; long-duration soak DEFERRED, not passed.**
The operator accepted continuing SBUS and subsequent component development before
revisiting the CANopen lifecycle and full-chain soak. This scheduling exception
does not waive short regressions, final acceptance, or hardware authorization.

## Later integration status — 2026-09-17

P9/P8-R/P10 bounded integration is now accepted; see [P10.3](P10_3_HIL_CHECKPOINT.md).
The locked cross environment has been restored and the P10.3 artifact has current
cross/ELF/source verification. This does not requalify a different P6 inhibitor
artifact. Latest recorded operator disposition is drive OFF after F6 A1; the
powered state and absent-cross notes below describe the September15 checkpoint.
P6 remains OPEN: old309s soak is still failed, V4 remains retired, and short JCAN
captures do not establish that its long-session USB framing defect is resolved.

## Current disposition (September15 historical checkpoint)

The bounded zero-target, independent-axis, revised stop/loss, moving SIGTERM,
userspace cable-inhibitor, drive-power V2 and composite X1 results remain accepted
within their exact artifact/fixture limits. Earlier failed/invalid trials remain
failed/invalid. The userspace inhibitor is not a kernel repair.

The three-hour soak v3 ran only 309.067 s and five passing cycles before its
capture exited. All 300 TPDO and 900 SDO speed samples were zero; the captured
9215 frames passed the existing traffic checks. Missing duration and final
capture coverage prevent a soak pass. SIGHUP behavior was reproduced without CAN
transmission; the old exit code/signal sender was not recorded.

V4 was staged but never ran. Its JCAN preparation rejected a USB packet whose
length disagreed with its header and exited. On deferral, the unused v4
authorization was retired locally and on RK3588. No consumed runner was reused.
See [the soak investigation and deferral](P6_SOAK_SESSION_REPAIR.md).

Latest operator confirmation for the preparation was drive powered, X1 locked,
wheels raised/stationary and wiring unchanged. Last interface inspection was
can0 UP/ERROR-ACTIVE with zero current error counters. No subsequent physical
power-off confirmation is recorded. Older OFF/DOWN statements belong to their
dated trials. Archiving/CI does not change physical CAN or drive state.

## Historical acceptance table — September 15 (see current update above)

| Item | Status | Evidence and limits |
| --- | --- | --- |
| P6.1–P6.4 contract, protocol, executor and zero-target lifecycle | PASS | Accepted historical inventories and bounded trials |
| Independent first-motion and earlier stop trials | HISTORICAL PASS | Exact earlier artifacts; not a pass for revised synchronous loss tests |
| Synchronous packed SDO left +5 rpm / 3 s | PASS | Nonzero TPDO feedback; operator-confirmed selected-wheel motion and normal stop |
| Single-RPDO left and right +5 rpm / 3 s | PASS | Each wheel tested separately; normal stop, other wheel stationary, no abnormal sound; original mapping restored |
| Latest software qualification | LOCAL CI PASS; CROSS/TARGET NOT REVALIDATED | Debug and ASan/UBSan 68/68 each; defaults 28/28 each; commissioning 36/36; script/static checks pass. New helper length validation has no new HIL; locked cross image is absent. See current review/CI record |
| Revised synchronous watchdog | PASS | One trial, 168 matching dual-capture frames, 1503.019 ms TX quiet; first zero-speed TPDO 634.003 ms after last request; operator confirms normal stop without restart |
| Revised synchronous heartbeat loss | PASS | One trial, 195 matching frames, target-to-zero request 601.994 ms, zero error/drop counters; operator confirms normal stop without restart |
| Revised synchronous TPDO loss | PASS | One trial, 171 matching frames, target-to-zero request 306.001 ms, timers restored, zero errors/drops; operator confirms normal stop without restart |
| Revised Shutdown | PASS | 164 matching frames; stop command at 1001.063 ms; trailing observed zero TPDO 240.099 ms later; operator confirms normal stop |
| Revised Disable Voltage | PASS | 164 matching frames; stop command at 1000.999 ms; trailing observed zero TPDO 240.688 ms later; operator confirms normal stop |
| Revised Quick Stop | PASS | 151 matching frames; stop command at 1001.046 ms; zero TPDO 289.540 ms later; operator confirms normal stop |
| Revised NMT Stop | STARTUP FAIL; STIMULUS NOT EXECUTED | 172 matching frames; zero nonzero-target requests; remained Quick Stop Active; startup and cleanup Shutdown state waits timed out |
| Quick Stop zero-target recovery repair | SOFTWARE + PHYSICAL PASS | One Disable Voltage recovery, 122 matching frames, all speeds/targets zero, cleanup verified; host and sanitizer 60/60; cross/ELF and target vcan pass |
| Historical NMT Stop after accepted zero recovery | FAIL; OPERATOR CONFIRMS STOPPED/SAFE | 147 matching frames; stop sent at 1000.889 ms; Pre-operational 0.149 ms later; no Stopped heartbeat or fresh final zero TPDO; cleanup state timeout |
| Repaired NMT Stop with Pre-operational SDO cleanup | PASS | 140 matching frames; Stopped heartbeat at 98.014 ms, Pre-operational at 99.994 ms, three fresh zero speeds by 357.420 ms; dual 0x14601460; operator accepted normal stop without restart |
| Moving SIGTERM | PASS | 135 matching frames, verified cleanup and operator acceptance; [record](P6_EXTERNAL_LOSS_TESTS.md) |
| Controlled interface down/up | PASS | V5 on September 14: 3003.580 ms hold, first recovery request packed zero, final dual 0x1460 and three zero speeds, operator accepted; 221 target frames matched within 11525 JCAN frames; earlier failed attempts preserved |
| Cable loss | FAIL; CLEANUP UNVERIFIED | One September 14 trial: controller errors, zero cleanup timeout; operator confirms stop/no restart and subsequent drive power-off. Offline review only; no retry |
| Userspace `can0` inhibitor | SOFTWARE + TARGET + PHYSICAL PASS | Real CAN error followed by verified interface down in 12.563 ms; zero later RK3588 requests across a conservative 24 s silent-JCAN window; no auto-up; operator accepted stop/no-restart |
| Drive-only power loss/restoration | PHYSICAL PASS; RUNNER POST-ASSERTION DEFECT | V2: 352 exact matching frames; normal-mode JCAN ACK with zero JCAN data-frame commands; restored boot to packed zero 2.494 ms; final `0x14401440` and three zero speed views; application exit 0; operator accepted; final drive OFF and `can0` DOWN. Attempt 1 remains invalid |
| Applicable fault and soak tests | X1 PASS / SOAK DEFERRED, NOT PASSED | Zero-motion V2: 1989 exact frames and powered reset/no-restart. Moving: 225 exact frames, left speed always zero, X1 low-half activation, stable right zero 99.902 ms later and 861.245 ms before scheduled target zero; operator accepted normal stop/no sound/no restart. Raw runner false is a retained typed-marker timeout. V3 failed after 309 s / five cycles; V4 never started. Operator deferred soak on September 15 until SBUS/full-chain integration; fault/bus-off injection remains unavailable |
| Simultaneous nonzero wheels, negative RPDO motion, loaded operation | NOT QUALIFIED | Outside the three successful physical repair trials |
| P6.7 final phase acceptance | OPEN | Remaining physical gates and final regression required |

## Source review and CI

[September 15 review and CI record](P6_CHECKPOINT_REVIEW_20260915.md) records the
source scope, one corrected inhibitor protocol defect, current checks and
unavailable validation. Historical HIL does not certify this new helper binary.
GitHub Actions runs host/static, Debug and sanitizer qualification with mandatory
managed vcan; it never runs physical RK3588 HIL. The run attached to the pushed
commit is the authoritative remote-CI result.

## Evidence and remaining limits

Use [the evidence index](evidence/README.md) and its verified archive manifest to
locate raw captures, software logs, original authorizations and operator reports.
Per-file hashes preserve both direct and archived bytes. In particular:

- [NMT Stop repair](P6_NMT_STOP_REPAIR.md) retains the failed startup/cleanup
  attempts and the later accepted 140-frame trial.
- [External loss](P6_EXTERNAL_LOSS_TESTS.md), [cable repair](P6_CABLE_LOSS_REPAIR.md)
  and [userspace inhibition](P6_USERSPACE_CAN_INHIBITOR.md) retain failed and
  accepted attempts separately.
- [Delayed TX](P6_DELAYED_TX_INVESTIGATION.md) and
  [driver-worker review](P6_ROCKCHIP_TX_WORKER_REVIEW.md) leave exact kernel
  worker/stop concurrency and the full matching kernel commit unresolved.
- [Power V2](evidence/p6_power_loss_v2_20260914/RESULT.md) and
  [X1 evidence](P6_EMERGENCY_INPUT_AND_SOAK_PREPARATION.md) preserve wrapper
  assertion defects without relabeling them as application failures or passes.
- Mechanical brake output is not applicable to this fixture (operator confirmed
  September 14). Non-destructive fault/electrical bus-off stimuli are unavailable.

## Next work

1. Continue SBUS and command/safety integration with short unit, vcan and bounded
   integration regressions. This checkpoint implements none of that later work.
2. Resolve JCAN USB receive framing before the next independent capture. Once the
   full chain is ready, separately authorize both CANopen lifecycle and full-chain
   long-duration tests; regenerate artifacts/preflight rather than reusing runners.
3. Complete final-source cross/target and outstanding acceptance requirements
   before declaring Phase 6 or the integrated system complete.

Production motion services, ROS2, loaded operation, persistent drive changes,
automatic motion reauthorization and production deployment remain outside scope.
