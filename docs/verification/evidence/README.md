# Phase 6 evidence index

Current milestone decisions are in [the checkpoint](../PHASE6_CHECKPOINT.md).
Raw captures, operator observations and rollback records for the accepted trials
and all four watchdog attempts remain directly readable below. Historical
preflights and superseded preparations are stored in verified archives.
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
