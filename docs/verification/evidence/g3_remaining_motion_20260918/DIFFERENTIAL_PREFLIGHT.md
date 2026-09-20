# Differential manual-input preflight — blocked (2026-09-20)

No hardware operation or runtime edit. Unchanged123 compiled inputs match the
accepted9b336936 snapshot. Both strict differential profiles accept the final
reference input but reject intermediate gradual inputs after authorization.

| Direction | Path | First rejected logical targets (rpm) |
| --- | --- | --- |
| Left | Steering then throttle | (0,3) |
| Left | Throttle then steering | (3,3) |
| Left | Simultaneous linear ramp | (0,3) |
| Right | Steering then throttle | (3,0) |
| Right | Throttle then steering | (3,3) |
| Right | Simultaneous linear ramp | (3,0) |

The existing Source3rpm output deadband and strict1..5rpm unequal-wheel gate
explain these transitions. ControlLoop::step rejects the enabled candidate
before RuntimeSession submission; the wire MotionGate independently enforces
the same predicate. Once stopped, reaching a later valid endpoint cannot restart
the consumed window. This is not a CAN/drive failure and does not show that all
possible sampled human gestures are impossible. A fast diagonal jump that
skips intermediate samples is not established as a reliable manual procedure.

Validation:

    python3 -B docs/verification/evidence/g3_remaining_motion_20260918/check_differential_paths.py
    ctest --test-dir out/build/p103-control-hil --output-on-failure -R '^(sbus_source_contract|control_hil_motion_gate|sbus_source_archived_replay)$'

All six paths reproduce rejection; existing host checks3/3 pass. No C++ change,
rebuild, cross build, deployment or motor run was performed for this preflight.

## Proposed next step — not implemented or physically authorized

Limit the change to differential qualification: allow positive equal-wheel
approach, then gradually introduce the selected steering direction. Continue
rejecting single-wheel, reverse, opposite differential and out-of-range targets.
Require actual unequal-wheel command evidence and appropriate feedback before
claiming differential acceptance; straight motion alone cannot pass the trial.
Keep the3s hard limit from FIRST nonzero command,2950ms cutoff,<=5rpm limits,
fresh neutral/rearm, +/-2rpm stable band and existing shutdown/restoration rules.
Update owner/wire checks, qualification success criteria and independent auditor
together; add transition and straight-only rejection coverage before changing
semantics. A new verified artifact requires target smoke/zero prerequisite and
a fresh bounded trial. This expands the present strict target relationship and
is not merely a rerun of the old test.
