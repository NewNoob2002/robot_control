# One-hour A2 — ACCEPTED; original strict capture order FAIL preserved

Archived and independently rechecked on 2026-09-18. This is continuous real-SBUS
zero-target ControlLoop operation with unloaded wheels, using control bec74e79
and diagnostics 9a226adb. Both one-shot markers are consumed. Operator START
and subsequent OFF/stationary/no-abnormal-sound confirmation are preserved.
On 2026-09-18 the user explicitly confirmed acceptance: “确认验收，规划下一步工作”.
See acceptance.json. G2 is accepted within this zero-target endurance scope,
including the disclosed capture-order exception; this is not a general change
to future capture criteria or permission for another hardware run.

| Check | Observed result |
| --- | --- |
| Continuous control | 3600.367 s; 360001 cycles, 359644 enabled cycles |
| SBUS and diagnostic records | 514256 valid frames; 1760263 ordered records |
| RPDO output | 360002, all exact-zero targets |
| Measured feedback | Within approved +/-2 rpm, including wire readbacks |
| Timing | 3 missed periods; maximum lateness 36.913 ms; maximum cycle 1.034 ms |
| Resources | 3559 samples; post-ready RSS 1668 KiB throughout; sampled CPU peak 5.997% |
| CAN health | Before/after and every resource sample pass health comparison |
| Restoration | 36 SDO downloads; 13 changed objects match original readbacks |
| Termination | Application/collector exit 0; no forced kill or lease failure |
| Dual capture | 518836 frames each; equal per-ID and per-direction sequences |

Timing stays within the recorded authorization budgets; this is not hard-real-time
qualification. JCAN reports INTERRUPTED because the host requests STOP after
target completion, before its 3780-second capture ceiling; error is null, shutdown
acknowledged and process exit 0. Target result's drive_power_off_confirmed=false
predates the separately recorded host operator-off.json confirmation.

## Capture-order exception

The original strict global-order audit FAILED and remains failed. At zero-based
positions 116748..116750, target candump records 201/201/281; JCAN records
281/201/201. Both 201 payloads are 0f0000000000 (enabled, zero targets), and the
281 payload is 0300000000. Only two positions differ because the two RPDOs are
identical. Everything before and after this three-frame window matches exactly;
all per-ID payload sequences and each direction's sequence match exactly.

Target timestamps span 1789718102.812666 to 1789718102.824492 (11.826 ms).
JCAN frame timestamps are zero, so this evidence cannot identify actual wire
timing or establish the cause of the ordering difference. Do not label it a
proven driver/capture defect or silently replace global-order acceptance with
per-direction acceptance. Functional checks PASS. The original pending status in reviewed-analysis.json is retained as the pre-acceptance audit result; acceptance.json records subsequent user acceptance without changing raw evidence or the failed check.

## Preservation and reproduction

target-result.tar.gz retains the original target manifest, raw trace, resource
samples, captures and results. All manifest members were SHA256-verified during
replay. Host evidence was compared byte-for-byte with out/ originals, which
remain untouched. JCAN's 75821975 raw bytes are losslessly compressed;
raw-storage.json records original/compressed hashes. SHA256SUMS covers this archive.
Original and reviewed audit scripts/logs are preserved separately.

For offline replay, copy this directory to a scratch directory named hour_a2,
decompress jcan/session.jsonl.gz there, and run from the repository root:

```sh
python3 /tmp/sbus-soak-review/hour_a2/audit-reviewed.py /tmp/sbus-soak-review/hour_a2 3600
```

The archived scripts retain their original workspace path. On another checkout,
adjust that path only in the scratch copy. The reviewed script asserts this
specific three-frame exception; it is not a general replacement acceptance rule.
The original script reproduces strict-order failure in a fresh scratch copy
without an unpacked directory. Never execute archived start_once/capture runners.

No new hardware run, deployment, interface change or source change accompanies
this archive. Moving/loaded endurance, production, P6 closure and delayed-TX
kernel repair are not established.
