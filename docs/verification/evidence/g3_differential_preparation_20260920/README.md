# G3 differential approach — software preparation complete (2026-09-20)

User approved a differential-only straight-to-turn approach after six gradual
input paths were shown to hit the old strict gate. New artifact SHA256:
51f94c1f51945f3d28fa945d58f14cb15ad4ec7a4cab353331a2f39e7ca16451.
This is software/device-free validation, not physical differential acceptance.

## Resulting behavior

Differential-left permits positive left<=right; differential-right permits
positive right<=left. The shared predicate covers owner submission, HIL envelope
and the independent wire gate. Source calibration/mixing/deadband is unchanged.
Single-wheel, reverse, wrong-side differential and >5rpm targets remain rejected.

Normal success requires at least two distinct, fresh owner-observed feedback
samples after an admitted unequal-target command and before stop, with both
wheels above2rpm forward and the selected measured speed ordering. Equal-wheel
commands/feedback alone cannot pass. The offline auditor independently correlates
raw successful RPDOs and TPDOs on application and capture clocks.

The3s hard limit/2950ms cutoff begins at the FIRST nonzero command, including
straight approach; introducing steering never resets it. Zero closes the window,
no restart, fresh neutral/rearm, +/-2rpm stable stop within1s with>=150ms+fresh
frame, +/-7.5rpm hard feedback and full volatile restoration remain enforced.
Existing online startup/stop guards and retrospective transient rules are unchanged.

## Verification

- Tests first: old implementation fails the new motion-gate test with40 failures;
  the independent auditor rejects equal-wheel approach. Recorded in red-test.log.
- Debug53/53 pass,531.64s; ASan/UBSan53/53 pass,540.06s. No skips/failures.
- Each configuration includes two14-case differential vcan/PTY matrices, one per
  right-axis polarity: gradual approach, first-command deadline, straight-only,
  equal/wrong differential feedback, opposite command and approach-time failsafe.
  Existing20-case G3 matrices per polarity, other motion/recovery/UART suites pass.
- Pure audit covers both directions/polarities plus40 mutations: absent, wrong,
  too few or noncausal differential samples, single-wheel, opposite target,
  extended-from-turn timing and restart. Unit gate checks include zero closure,
  polarity, invalid boundaries and restarting the unequal interval after equality.
- clang-format22.1.8 and diff checks pass. Scoped clang-tidy22.1.8 covers five
  affected translation units; only the existing Result::value optional-access
  advisories in platform/linux/error.hpp97/106/115 remain.
- Locked offline Docker cross build against validated actual RK3588 sysroot and
  both HIL/diagnostics ELF audits pass.123 compiler dependency inputs match the
  frozen snapshot and current source. Cross toolchain/sysroot metadata retained.
- Target machine identity/hash and five device-free smoke checks pass: help,
  zero/differential modes with a nonexistent UART, missing physical polarity,
  and rejecting a5s differential window before device activation.

Sandbox attempts could not access namespace/Docker facilities; scoped escalated
verification succeeded. The initial Docker sandbox error is retained in cross.log;
the successful cross build is cross-escalated.log. No toolchain lock was changed.

## Reproduction and artifacts

    cmake --build out/build/p103-control-hil --parallel 2
    cmake --build out/build/p103-control-hil-san --parallel 2
    python3 -B scripts/test/test_control_hil_differential.py
    ctest --test-dir out/build/p103-control-hil --output-on-failure -R 'control_hil_differential|control_hil_motion_gate'

Full ctest logs/XML contain individual cases/results. cross-build.py records
the exact CMake/container invocation. Snapshot:
out/g3-differential-v1-cross-source; output:
out/build/cross/g3-differential-v1-runtime/tools/control_hil/robot-control-hil.
New target staging directory:
/home/cat/.cache/robot-control/staging/g3-differential-51f94c1f-20260920.
Prior9b336936 staging/artifacts and all consumed trials remain intact.

## Remaining physical prerequisites

New artifact still needs a separately coordinated zero-target prerequisite,
then fresh bounded left/right differential trials. Suggested motion sequence:
neutral/new CH6/Enabled-zero, steering neutral and full forward throttle first,
then a small selected-side steering input. Steering-first remains unsupported
because it can produce a single-wheel candidate. No new motor run, production
activation, persistent drive change or physical trial permit is included here.
