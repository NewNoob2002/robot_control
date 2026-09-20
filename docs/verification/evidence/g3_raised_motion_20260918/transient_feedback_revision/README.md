# Right-negative A1 — PASS under user-approved transient-feedback rule

Date:2026-09-18. Current acceptance: **PASS_USER_APPROVED_START_STOP_TRANSIENT**.
The user explicitly instructed that these few startup/stop feedback jumps are
normal, requested synchronization into documentation/constraints, and confirmed
right-negative acceptance. Exact instruction and artifact/evidence identities
are in acceptance.json. Left-negative A1 remains PASS; both negative single-wheel
items are now accepted within their raised/unloaded scope.

## Evidence supporting the revised acceptance

The right-negative trial has one+2.9rpm sample78.847781ms after zero send
(wire78.222036ms). It is classified NORMAL_START_STOP_FEEDBACK_TRANSIENT,
not an independently observed mechanical reversal. Motion lasted2950.092712ms,
with correct negative targets, no positive/left target and no restart.58 fresh
right-direction samples were obtained. Stable+/-2rpm stop is verified378.481420ms
after zero, including the continuous150ms hold plus another fresh frame.
1466 dual-capture frames match globally;36 allowed volatile writes and complete
baseline restoration verified. Left feedback stays0 and raw right-6.1..+2.9rpm
is below the7.5rpm hard bound. CAN counters unchanged. Operator confirmed
right reverse/stop normal, left stationary, no abnormal sound and drive OFF.

The general acceptance constraints are now in
[G3 trial rules](../../../../plans/G3_RAISED_MOTION.md#启停短暂反馈跳变验收规则)
and the root AGENTS.md coding rules. Record phase/count/peak/timing; do not
invent a universal frame-count ignore window. Stable standby/cleanup and
uncommanded-wheel+/-2rpm, hard feedback/command bounds, direction evidence,
bounded stop, restoration and independent captures remain required. Sustained
wrong-direction/overspeed or other safety failures are not classified normal.
This retrospective revision accepts the recorded right-stop sample; it does
not claim that additional startup or dual-wheel cases have been tested.

## Historical result and runtime boundary

The original right_negative_a1 directory and its42-entry SHA256SUMS are unchanged,
including README, application exit1, feedback_bad=1, feedback_hard_bad=1,
primary_ok=0 and FAILED_FEEDBACK_CRITERION in failure-analysis.json. Those are
correct records of the original guard, not the current revised acceptance.
The earlier unaccepted status is superseded only by this separate decision.
No runtime source/ELF, online guard/filter, persistent parameter, interface or
hardware state changed; no new run occurred.5f44146c can still return its old
feedback failure. This update synchronizes acceptance constraints, not deployed
online behavior. All consumed runners remain consumed; future physical trials
still need their own preparation and fresh readiness.

## Actual verification

    python3 -B docs/verification/evidence/g3_raised_motion_20260918/transient_feedback_revision/verify_revision.py

The offline verifier checks all42 original evidence checksums, replays the
original failure/protocol/stop auditor in a temporary directory, and compares
its output exactly with the archived failure analysis. It then verifies the
bounded sample, correct commands, hard bound, standstill, full restoration and
operator disposition supporting the new user decision. verification.json
records the new PASS and preserved original failure separately.

No new host/cross build or HIL is claimed; application software was not edited.
G3 dual-wheel/turn/differential and corresponding protection cases remain open;
no ground/loaded, production or retry authorization follows from this revision.
