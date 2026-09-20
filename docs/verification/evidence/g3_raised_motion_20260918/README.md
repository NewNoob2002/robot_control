# G3 raised-motion evidence — 2026-09-18

**Current acceptance: new-artifact zero prerequisite and both negative single-wheel
items PASS.** Right-negative A1 is accepted under the user's brief startup/stop
feedback-transient rule; its original failed application verdict remains intact.
See the [acceptance revision](transient_feedback_revision/README.md).

Artifact5f44146c is staged on the identified RK3588.123 compiled inputs,
binary/UART/target identity and device-free smoke passed. Scope is raised/unloaded
bounded qualification, not production or loaded/ground acceptance.

| Trial | Current acceptance | Evidence |
| --- | --- | --- |
| Zero A1 | FAILED / INCOMPLETE, drive not powered | [Original](zero_a1/README.md): one status upload, no writes or control cycles |
| Zero A2 | PASS | [Record](zero_a2/README.md):20s,3230 matching frames,2002 zero RPDOs/full restoration |
| Left-negative A1 | PASS | [Record](left_negative_a1/README.md):2.960s,1629 matching frames,266ms stable stop/full restoration |
| Right-negative A1 | PASS under revised user-approved criterion | [Revision](transient_feedback_revision/README.md):2.950s,1466 matching frames,one+2.9rpm stop transient,378ms stable stop/full restoration |

[Original right-negative failure](right_negative_a1/README.md), raw flags, captures,
and all failed audits remain unchanged. Its earlier unaccepted conclusion is
historical; acceptance.json in the revision is authoritative for the new decision.
Operator confirmed each selected wheel reverse/stop normal, opposite stationary,
no abnormal sound and drive OFF. Latest OFF is after right-negative A1, not future
powered readiness. All runners are consumed; this revision adds no hardware run.

Normal brief startup/stop feedback classification does not widen the stable
+/-2rpm band, change exact target bounds, bypass stable-stop/restoration gates,
or imply new deployed online behavior. Current5f44146c still reports the original
strict G3 trace failure for such an excursion. Future qualification follows the
[G3 acceptance constraints](../../../plans/G3_RAISED_MOTION.md#启停短暂反馈跳变验收规则).

G3 dual-wheel positive/negative, turning/differential and corresponding protection
cases remain pending. No automatic retry, endurance or kernel-repair claim.

## Latest: dual-positive A1 FAILED physical direction

[Dual-positive A1](dual_positive_a1/README.md) completed2.950s and full restoration,
with1200 matching frames and302ms stable stop. Operator explicitly reports left
vehicle-forward/right vehicle-backward. This is a physical direction mismatch,
not an accepted feedback transient; software exit0 is not physical acceptance.
Current+1/+1 binding requires vehicle-frame right-polarity correction across
encoding/decoding and HIL guards/audits before continuing. No source or persistent
parameter changed; no retry. Operator confirms drive OFF/both stopped/no sound.
Earlier single-wheel signed acceptance remains recorded but does not qualify
the now explicitly defined vehicle-frame right-forward mapping.
