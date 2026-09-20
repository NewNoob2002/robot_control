# G3 right-negative A1 — FAILED feedback criterion (2026-09-18)

Fresh local START authorized a single raised/unloaded right-negative trial with
unchanged5f44146c after zero A2 and left-negative A1 passed. Right targets-5..-1rpm,
left exact0, hard motion3s/cutoff2950ms, measured near-zero+/-2rpm unchanged.
Operator coordinated backward throttle/right steering from the live motion cue.

## Failure and separately verified cleanup

Automatic motion cutoff, stop cause2, occurs after2950.092712ms nonzero targets
(application monotonic TX clock; wire2950.387001ms). No positive right target
or nonzero left target; no restart.58 fresh right-negative feedback samples.
At78.847781ms after zero send, one right feedback sample is+2.9rpm, exceeding
the selected-wheel opposite-direction+2rpm limit. The identical sample appears
in both matching wire captures,78.222036ms after the captured zero RPDO.
Clock domains are kept separate. Trace reports feedback_bad=1/hard_bad=1;
control_hil_trace_failure, primary_ok=0, application exit1. This is a retained
FAILED criterion, not accepted internal-estimator noise or proof of physical
reverse motion. No criterion or binary was changed and no retry was started.

Subsequent dual-axis150ms stable+/-2rpm hold plus a fresh sample is independently
verified378.481420ms after zero send, within1s.1466 frames match exactly across
both captures; all trace TX agrees with wire traffic.36 allowed SDO writes,
complete original volatile baseline restoration, exact zero/Disable Voltage
are independently verified. Raw right feedback-6.1..+2.9rpm; left0 throughout.
CAN error/drop/controller counters unchanged. JCAN clean shutdown, no data send
or reconnect.765 cycles,607 enabled samples,4 missed periods/max lateness43.107ms;
internal shutdown22us; no hard-real-time claim.

Local OFF confirms right reverse/stop normal, left stationary, no abnormal
sound and drive OFF. These observations do not override the failed raw feedback
criterion. Host/target/capture one-shot markers remain consumed.

Raw evidence: physical-once/, target-result.tar.gz, jcan/session.jsonl and
operator-off.json. failure-analysis.json retains the verdict and separate
protocol/cleanup findings. Replay from repository root:

    python3 -B docs/verification/evidence/g3_raised_motion_20260918/right_negative_a1/audit-failure.py

The normal analyze-motion.py intentionally requires primary_ok=1 and must not
accept this failed trial. audit-failure.py copies the existing G3 protocol/
restoration checks into a clearly limited audit, preserving the original feedback
failure instead of bypassing it to manufacture motion acceptance.

Left-negative A1 is PASS. Right-negative remains unaccepted; no dual-wheel,
turn/differential, ground/loaded or new physical permit is implied.
