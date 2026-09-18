# RK3588 CANopen / JCAN actual-traffic soak — 2026-09-18

**ACCEPTED on2026-09-18 under the user-approved +/-2rpm standstill criterion.**
The operator confirms stationary wheels, no abnormal sound and drive power OFF,
and explicitly approves this trial. See `operator-acceptance.json` and
`accepted-analysis.json` for the current disposition. `joint-analysis.json`
retains the earlier strict-zero audit. Original runner results, raw captures
and the target archive remain unchanged.

- Target duration10807.299s,175 successful lifecycle cycles;10500 periodic
  feedback samples and31500 SDO velocity samples all satisfy their original checks.
- All348246 CAN frames match JCAN and target candump exactly, including order
  and payload. One JCAN connection,1078 healthy status responses, no malformed
  packet error/disconnect/reconnect, successful shutdown and identical config
  readbacks. No CAN error/drop counter increases at the sampled checkpoints.
- JCAN `INTERRUPTED` is the supervisor-requested capture stop after target DONE,
  not an error or a failed three-hour target window. Its independent185-minute
  maximum was a cleanup allowance. The actual-traffic JCAN window passes.
- The historical strict all-frame zero-feedback audit FAILED on9 of211221 TPDO1 frames:
  right feedback -0.3..+0.3rpm; left remains0. All lie within the previously
  approved +/-2rpm standstill band and are accepted. Details and timestamps are retained in
  `feedback-observations.json`; the original strict-zero failure is not rewritten.
  Periodic sampling missed these between-sample values, explaining target rc0.
- All TPDO states retain the observed X1 bit and disabled drive states. Protocol
  inspection finds only the permitted reads, NMT changes and volatile heartbeat
  writes; no RPDO, target or controlword writes.

This demonstrates non-recurrence of the USB length error during this observed
traffic window, not a proven root-cause repair. USB raw transfers were not
captured. This is CANopen lifecycle qualification, not an integrated
SBUS/ControlLoop, moving, loaded or production soak. P6 is not declared closed.
Operator stationary/no-sound/OFF confirmation is recorded. No new physical run,
automatic retry or interface change was performed for this criterion revision.

## Raw capture storage after node cleanup

The original jcan/session.jsonl bytes are stored in
[september18-capture-and-pilots.tar.gz](../archives/september18-capture-and-pilots.tar.gz).
The archive manifest retains the original path, byte count and SHA256
809cbc9c13d6c7c95ba2958695cd2820a6ddfebbd4a5d8a3fd0c88b6f9887907.
From the repository root, create a separate scratch directory, then extract:

    mkdir -p /tmp/robot-control-evidence-20260918
    tar -xzf docs/verification/evidence/archives/september18-capture-and-pilots.tar.gz -C /tmp/robot-control-evidence-20260918
    python3 scripts/test/test_phase6_evidence.py

Historical runners and analysis path strings intentionally keep their original
names. The target-result.tar.gz remains alongside this README. No raw bytes or
original verdicts were changed by compression.
