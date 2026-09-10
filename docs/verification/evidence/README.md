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
| Current tested software and RK3588 artifact | [p6_6_20260910_manual_tpdo_preparation](p6_6_20260910_manual_tpdo_preparation/) |
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
