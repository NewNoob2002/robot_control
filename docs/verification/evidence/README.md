# Phase 6 evidence index

Current milestone decisions are in [the checkpoint](../PHASE6_CHECKPOINT.md).
Raw captures, operator observations and rollback records remain available
directly or by their original member paths in the verified archives. Historical
preflights, superseded preparations and large follow-up logs are archived.
An identical pre/post configuration is two distinct observations and is retained.
Historical authorizations and runners are records, not permission to rerun hardware.

## Direct evidence

| Milestone / trial | Evidence |
| --- | --- |
| Synchronous packed SDO repair — left motion/stop operator-confirmed | [sync feedback](p6_sync_feedback_20260911/RESULT.md) |
| Single RPDO left/right — TPDO speed restored, mapping restored, both operator-confirmed | [left](p6_rpdo_feedback_20260911/left/analysis.json), [right](p6_rpdo_feedback_20260911/right/analysis.json) |
| Synchronous stop/loss software 60/60 + sanitizer 60/60; no physical execution after two approval timeouts | [remainder](p6_sync_remainder_20260911/execution_blocker.json) |
| Post-checkpoint safety/I/O software fixes — no new HIL | [p6_review_20260910_safety_io](p6_review_20260910_safety_io/) |
| Review fixes: non-actuating target checks | [round 1](p6_review_hil_20260910_round1/test_result.json) |
| RK3588 application executor: software checks pass; physical acceptance blocked | [executor results](p6_review_rk3588_executor_20260910/test_result.json) |
| RK3588 physical run: dual captures match, cleanup verified; diagnostics and zero-only feedback remain open | [postflight](p6_review_rk3588_executor_20260910/hil_prepared/postflight_verification.json) |
| RK3588 second collection: manual feedback/polarity and minor readings operator-accepted; diagnostics open | [second postflight](p6_review_rk3588_executor_20260910/hil_trial_2/postflight_verification.json) |
| Review fixes: independently verified 16-read inventory | [round 2 inventory](p6_review_hil_20260910_round2/inventory_verification.json) |
| Status-first manual feedback attempt 1 — aborted before sampling | [attempt 1](p6_review_hil_20260910_round2/manual_attempt_1/verification.json) |
| Status-first manual feedback attempt 2 — aborted on missing request/response | [attempt 2](p6_review_hil_20260910_round2/manual_attempt_2/verification.json) |
| P6.1 initial inventory | [p6_1_20260904](p6_1_20260904/) |
| P6.1 accepted complete inventory | [p6_1_20260904_continuation_1](p6_1_20260904_continuation_1/) |
| P6.3 software baseline | [p6_3_20260904](p6_3_20260904/) |
| P6.4 zero-target normal | [p6_4_20260907_zero_sequence_2](p6_4_20260907_zero_sequence_2/) |
| Revised synchronous remainder: six passes; NMT startup failure without motion | [remainder results](p6_sync_remainder_20260911/RESULT.md) |
| Quick Stop zero-target recovery: host/sanitizer 60/60, cross/ELF, target vcan and physical recovery pass | [repair record](../P6_ZERO_TARGET_RECOVERY.md), [results](p6_zero_recovery_20260911/test_result.json) |
| P6.4 post-enable SIGTERM at zero target | [p6_4_20260907_active_sigterm_180](p6_4_20260907_active_sigterm_180/) |
| P6.5 left +5 rpm, 500 ms | [p6_5_20260908_first_motion_retry_1](p6_5_20260908_first_motion_retry_1/) |
| P6.5 left +5 rpm, 3 s | [p6_5_20260908_three_second_trial_1](p6_5_20260908_three_second_trial_1/) |
| P6.5 right +5 rpm, 3 s | [p6_5_20260908_second_axis_trial_2](p6_5_20260908_second_axis_trial_2/) |
| P6.6 NMT Stop | [p6_6_20260908_nmt_stop_trial_5](p6_6_20260908_nmt_stop_trial_5/) |
| P6.6 Shutdown | [p6_6_20260909_shutdown_trial_3_10s](p6_6_20260909_shutdown_trial_3_10s/) |
| P6.6 Disable Voltage | [p6_6_20260910_disable_voltage_corrected_trial_1](p6_6_20260910_disable_voltage_corrected_trial_1/) |
| P6.6 Quick Stop | [p6_6_20260910_quick_stop_trial_1_capture_fixed](p6_6_20260910_quick_stop_trial_1_capture_fixed/) |
| Historical manual-TPDO software and RK3588 artifact | [p6_6_20260910_manual_tpdo_preparation](p6_6_20260910_manual_tpdo_preparation/) |
| Manual TPDO feedback — operator accepted | [p6_6_20260910_manual_tpdo_trial_1](p6_6_20260910_manual_tpdo_trial_1/) |
| Watchdog attempt 1 — setup failure | [p6_6_20260910_watchdog_trial_1](p6_6_20260910_watchdog_trial_1/) |
| Watchdog attempt 2 — confirmed stop/restart | [p6_6_20260910_watchdog_trial_2](p6_6_20260910_watchdog_trial_2/) |
| Watchdog attempt 3 — no initial motion | [p6_6_20260910_watchdog_trial_3](p6_6_20260910_watchdog_trial_3/) |
| Watchdog attempt 4 — motion/stop; timing open | [p6_6_20260910_watchdog_trial_4](p6_6_20260910_watchdog_trial_4/) |

## Historical records

Archives preserve original paths relative to this evidence directory.
Use [MANIFEST.json](archives/MANIFEST.json) to locate any historical path and
check its original bytes/hash. Archive checksums are in [SHA256SUMS](archives/SHA256SUMS).

| Archive | Original directories | Files |
| --- | --- | --- |
| [p6_4-history.tar.gz](archives/p6_4-history.tar.gz) | 13 | 177 |
| [p6_5-history.tar.gz](archives/p6_5-history.tar.gz) | 6 | 144 |
| [p6_6-history.tar.gz](archives/p6_6-history.tar.gz) | 16 | 223 |
| [p6_7-history.tar.gz](archives/p6_7-history.tar.gz) | 1 | 31 |
| [p6_review-software-20260911.tar.gz](archives/p6_review-software-20260911.tar.gz) | Software logs across review/repair directories | 42 |
| [p6_followup-large-evidence-20260915.tar.gz](archives/p6_followup-large-evidence-20260915.tar.gz) | Large September 11–15 follow-up captures and logs | 12 |

Inspect or extract into a separate directory; do not overwrite current evidence:

```sh
tar -tzf docs/verification/evidence/archives/p6_6-history.tar.gz
mkdir -p /tmp/phase6-history
tar -xzf docs/verification/evidence/archives/p6_6-history.tar.gz -C /tmp/phase6-history
```

Detailed historical verification documents may still name these original paths.
Those names identify archive members; they do not imply a missing trial.

## Consolidation record

On 2026-09-10, 36 superseded directories (575 files) were verified byte-for-byte
inside four archives before removing their loose copies. Four generated Python
cache files were removed. Three duplicate preparation runner files were removed;
their canonical copies remain in the executed manual-TPDO trial. The manifest
records archive members and duplicate replacements. No unique raw evidence was discarded.
A pre-cleanup local backup also remains under out/checkpoints/ (ignored by Git).

Two pre-existing NMT Stop report-hash discrepancies were reviewed without changing
those reports or their raw captures. See
[preserved checksum review](p6_6_20260908_nmt_stop_trial_5/CHECKSUM_REVIEW.json).

- [2026-09-11 application diagnostic regression](p6_review_diagnostics_20260911/): startup/reopen boot-up attempt and deadline-event diagnostics; see [report](../P6_REVIEW_DIAGNOSTICS.md).

- [2026-09-11 online startup validation](p6_review_online_20260911/): zero/first-motion attachment with no boot-up and heartbeat initially disabled.

## 2026-09-11 repair checkpoint cleanup

Forty-two software build/test/static logs (1,091,712 bytes) are now preserved
byte-for-byte in the 77,512-byte review archive. Existing SHA256SUMS still
validate those original paths through the archive; current raw CAN captures,
operator confirmations, failure reports, configuration readbacks and one-shot
markers remain directly accessible. Earlier claims in dated reports describe
their own artifact, not the latest source.

The [workspace cleanup ledger](archives/WORKSPACE_CLEANUP_20260911.json) records
2,885,747,660 bytes of removed generated build/snapshot material. Before
removal, 21,045 historical snapshot files were verified in a deduplicated local
archive under out/checkpoints/ (78,270,456 bytes, ignored by Git). Current
qualification/default builds, recent cross artifacts, staging, sysroots and the
separate review checkout were retained. No hardware operation was performed
as part of archiving or cleanup.

## NMT Stop feedback/cleanup repair — 2026-09-11

See [verification record](../P6_NMT_STOP_REPAIR.md),
[software and artifact manifest](p6_nmt_stop_repair_20260911/manifest.json),
[test results](p6_nmt_stop_repair_20260911/test_result.json), and
[accepted physical analysis](p6_nmt_stop_repair_20260911/physical_nmt_stop_once/analysis.json).
The bundle preserves red/failed fixture runs, final software logs, raw dual
captures, authorizations, one-shot marker and operator acceptance.

## P6.7 external loss work (2026-09-11)

See [record](../P6_EXTERNAL_LOSS_TESTS.md) and p6_external_loss_20260911/: moving_sigterm_once passes; interface_loss_once is an aborted permission failure with verified cleanup; external_recovery_v1 contains target/software artifact evidence. At that initial September 11 checkpoint, no cable or power trial had run; the later September 14 results are indexed below.

V4 on 2026-09-14 did not trigger interface loss; 301 matching frames and verified
zero/cleanup are preserved in interface_loss_manual_v4_once. The newly authorized
[v5 extended-window trial](p6_interface_v5_20260914/RESULT.md) passes one physical
interface-loss run with verified recovery/cleanup and operator acceptance. Its
221 target frames match in order within 11525 JCAN frames; extra frames are
repeated TPDO1. Source, artifact, regression, raw captures and consumed markers
remain separate from earlier failed attempts.

The subsequent [CAN-branch cable-loss trial](p6_cable_loss_20260914/RESULT.md)
failed with controller errors and unverified zero cleanup. Consumed runners,
raw captures and operator stop/no-restart confirmation are preserved. The drive
was subsequently confirmed powered off; hardware work is paused for recovery
review. Emergency-stop/fault-method availability is also recorded.

## September 14 powered-off cable repair

See [software repair and validation](../P6_CABLE_LOSS_REPAIR.md). Host and sanitizer 62/62, commissioning 36/36, default Debug/Release 28/28 each and clean RK3588 cross/ELF pass. No target deployment or physical operation was performed. The failed cable trial remains failed; all consumed runners remain consumed. Reopen log repetition is repaired offline.

## Powered-off delayed-TX investigation

[Investigation](../P6_DELAYED_TX_INVESTIGATION.md) correlates the late read-only request with a TX increment after application exit and demonstrates queued delivery after socket close on isolated host vxcan. Virtual interface-down prevents delivery in that host setup. The target virtual experiment is unavailable; exact rockchip_canfd queue/stop behavior remains unresolved. No production or physical-interface changes were made, and the cable trial stays failed.

## Target driver retry review

[Machine-code-backed review](../P6_ROCKCHIP_TX_WORKER_REVIEW.md) confirms a driver TX worker that resubmits and self-schedules, absent advertised one-shot support (mask0x17), and cancellation after stop/runtime-PM release in the target device-close path. Disk-image notes match the running kernel. No kernel build/install or physical CAN operation occurred. The original full-tree commit and physical repair remain open; the failed trial is unchanged.

## Userspace CAN interface inhibitor

[Implementation and validation](../P6_USERSPACE_CAN_INHIBITOR.md) adds the
Debug-only/default-OFF `can0` inhibitor and retains test-first, host, sanitizer,
static, cross and ELF evidence in
[p6_userspace_can_inhibit_20260914](p6_userspace_can_inhibit_20260914/). Target
deployment and isolated-vcan evidence, plus the invalid no-disconnect first
attempt, are in
[p6_userspace_can_inhibit_target_20260914](p6_userspace_can_inhibit_target_20260914/).
The accepted operator-synchronized physical retest is in
[p6_userspace_can_inhibit_retest_20260914](p6_userspace_can_inhibit_retest_20260914/):
one controller error, `can0` DOWN 12.563 ms later, all 171 target normal frames
matched in silent JCAN, zero later RK3588 requests through a conservative 24 s
window, and operator-confirmed normal stop/no restart. The later 100000-frame
bound consists only of repeated drive-side TPDO1 and did not cause a retry.

## Drive-only power loss/restoration

[p6_power_loss_20260914](p6_power_loss_20260914/) contains the consumed one-shot
runner, target staging manifest and reviewed
operator sequence for right `+5 rpm`, drive-only power removal, one power
restoration with no restart, and final drive power-off. The target scripts and
existing qualification/helper hashes were verified before execution. The first
attempt is retained in
[p6_power_loss_20260914](p6_power_loss_20260914/RESULT.md) as invalid: application
zero/interface-down preceded the power cut and silent JCAN exceeded its frame
bound on repeated drive boot heartbeat.

[p6_power_loss_v2_20260914](p6_power_loss_v2_20260914/RESULT.md) preserves the
accepted V2 result. JCAN `normal --receive` actively supplied link-layer ACK but
submitted zero data-frame commands. Both captures contain the same 352 frames;
the first RK3588 request after the restored drive boot was packed zero 2.494 ms
later. Final state was `0x14401440` with three zero speed views, the application
exited 0, the operator accepted stop/no-restart behavior, and final cleanup left
the drive OFF and `can0` DOWN. The raw wrapper false result remains preserved and
is classified as a post-run assertion defect. No retry was performed. Active ACK
changes the bus error conditions, so this evidence applies to drive-power loss
and does not replace silent capture for cable-loss or inhibitor qualification.

## X1 emergency input and overnight soak preparation

[p6_emergency_input_20260914](p6_emergency_input_20260914/) contains the
consumed first zero-motion X1 runner, exact traffic and statusword checks,
operator sequence, powered-off interface restoration/finalization, and local
parser tests. The separate
[preparation record](../P6_EMERGENCY_INPUT_AND_SOAK_PREPARATION.md) documents
the 1-12 hour target soak runner and its single-file archive/analyzer workflow.

The authorized 2026-09-15 attempt stopped before any X1 action.
The only CAN exchange was a read-only `0x6041:00` request/response returning
`0x14001400`; the qualification guard falsely rejected CiA402 Not Ready to
Switch On as enabled. [Attempt 1](p6_emergency_input_20260914/RESULT.md) is
consumed and retained. The software repair is validated, but a new target and
physical authorization is required.

[p6_emergency_input_v2_20260915](p6_emergency_input_v2_20260915/RESULT.md)
contains the accepted zero-motion result: 1989 exact target/JCAN frames, 1206
zero-speed TPDO1 samples, low-half bit 15 `0→1→0`, stationary high half,
operator-confirmed no motion/sound, and final OFF/DOWN. The runner's false
two-half assertion is retained; offline analysis classifies the test as pass.

[p6_emergency_input_moving_20260915](p6_emergency_input_moving_20260915/RESULT.md)
contains the consumed moving X1 result. RK3588 and silent JCAN retained the same
225 frames. The right wheel moved, the left stayed zero, low-half X1 bit 15
activated, and stable right zero followed 99.902 ms later, 861.245 ms before the
scheduled packed zero target. The operator accepted normal stopping, no abnormal
sound or restart, and final safe power-off. The raw wrapper false is retained as
a typed `ESTOP_LOCKED` marker timeout; 21 active TPDO samples prove the physical
press occurred in time. Powered reset/no-restart evidence comes from the V2
zero-target result, so no moving retry is required.

[p6_zero_motion_soak_20260915](p6_zero_motion_soak_20260915/PREPARED.md)
contains the staged, authorization-disabled three-hour soak runner. It has a
dedicated one-shot entry and fresh powered-off `can0` restore/finalize helpers,
so it does not reuse any consumed X1 runner. It records continuous candump,
per-cycle application logs, CAN interface statistics, result and manifest files,
then emits one tar.gz archive for next-day offline analysis. This describes the
original preparation; subsequent attempts and the current deferral are below.


## September 15 checkpoint: soak deferred and evidence cleanup

The [soak disposition](../P6_SOAK_SESSION_REPAIR.md) preserves the v3 failed
three-hour attempt (309 s, five passing cycles) and the
[v4 not-started result](p6_zero_motion_soak_v4_20260915/RESULT.md). JCAN preparation
failed on a USB receive-length mismatch; the target soak never ran. Original
preparation documents remain as *.prepared.json; current v4 authorization is
retired both locally and on RK3588. Future SBUS/component work is permitted by
the operator's scheduling decision, but soak remains unpassed and Phase 6 open.

The [review/CI record](../P6_CHECKPOINT_REVIEW_20260915.md) and
[checkpoint results](p6_checkpoint_20260915/RESULT.md) describe the final source.
The [cleanup ledger](archives/WORKSPACE_CLEANUP_20260915.json) records 12 large
files (58,452,046 bytes) preserved byte-for-byte in a 277,915-byte archive, 68
removed generated Python cache files, and verified local backups before old
build/snapshot cleanup. Extract archived captures to a separate directory when
rerunning historical analyzers; do not overwrite current evidence.

## P10.2 software/vcan closure

[Control-loop baseline](../P10_2_CONTROL_LOOP_BASELINE.md) and
[raw evidence](p10_2_control_loop_20260916/README.md) record PTY-to-RPDO closure,
independent virtual feedback, fault/restart/shutdown injection, P6 regression and
locked aarch64 build. Initial failures remain preserved. No physical test or
future hardware authorization is included.

## September 18 node: actual-traffic soak accepted and policy delivery

- [P10.3 delivery](../P10_3_DELIVERY_BASELINE.md): bounded unloaded F1–F6
  accepted; source commit 55617ec passed remote CI run 35216344992.
- [Static-analysis policy](clang_tidy_20260917/README.md): root clang-tidy
  adopted across 69 first-party translation units; 33 advisory findings retained.
- [Soak preparation](soak_preconditions_20260917/) retains the runner cleanup
  and watchdog checks. [Empty-bus capture](jcan_idle_soak_20260918/README.md)
  was interrupted at 345 seconds with zero frames, not accepted as a long soak.
- [RK3588/JCAN soak](rk3588_can_soak_20260918/README.md): 10807.299 seconds,
  175 cycles and 348246 matching frames; operator accepts the observed feedback
  under +/-2rpm. Original strict-zero audit failure remains unchanged.
- [Standstill policy](standstill_policy_20260918/README.md): physical CLI and
  soak analysis use +/-2rpm measured feedback, while commands remain exact.
  Debug and sanitizer each pass 78/78; focused Control HIL, static analysis,
  locked aarch64 build and ELF audit pass. New artifact is not deployed.

P6 remains open: this disabled CANopen lifecycle test does not qualify continuous
SBUS-to-ControlLoop operation, loaded motion, production or the kernel repair.
The operator's latest recorded disposition is stationary/no abnormal sound/OFF.
All historical failed/incomplete trials and consumed authorizations are retained.

[Cleanup ledger](archives/WORKSPACE_CLEANUP_20260918.json) records seven files
(52718408 bytes) preserved in a 481749-byte archive after byte-for-byte verification.
The existing Phase 6 evidence checker validates its manifest and every member.
Extract to a separate scratch directory for historical replay; never execute
consumed runners. See the soak README for the extraction command.
