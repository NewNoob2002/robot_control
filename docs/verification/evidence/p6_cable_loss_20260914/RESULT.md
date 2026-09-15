# RK3588 CAN-branch cable-loss test — failed 2026-09-14

Physical execution failed; protocol cleanup is unverified. The operator requested
continuation of remaining tests after the accepted v5 interface-loss run.

This is a separate, one-shot physical branch-disconnection stimulus. It reuses
the exact verified v5 ELF 2304c1c9892d327f73c65304601adfd4ced30e81fb2d4f3bfd1e2c5966ae857c
and unchanged C++ source. Host and sanitizer 62/62, clean cross/ELF and target
isolated-vcan evidence remain in ../p6_interface_v5_20260914/. No rebuild or
repetition of those unchanged-code tests is necessary for this wrapper change.

## Exact sequence

- Raised/unloaded wheels; one right +5 rpm packed target, left zero; eight-second
  deadline before loss, one-second drive watchdog, ten-second passive recovery.
- The operator is at the RK3588 CAN branch connector before reporting READY.
  After right-wheel motion is observed, allow approximately one second of lead
  before disconnecting that branch. Keep the drive and silent JCAN connected
  and powered; do not change can0 configuration or disconnect the drive branch.
- Confirm stopping; reconnect approximately three seconds after disconnection.
  If stopping is abnormal, use the independent power cut and do not reconnect
  as part of this trial. No action relies on chat-message timing.
- Recovery may issue packed zero and verified disabled-state cleanup only,
  restoring temporary heartbeat/watchdog baselines. No re-enable or retry.
- Both captures remain bounded to forty seconds/100000 frames. Success requires
  raw protocol review, operator confirmation of physical disconnection/recovery
  and safe stopped wheels; application exit alone is insufficient.

The new wrapper has no privileged interface helper or link-state mutation.
Unexpected capture diagnostics, nonzero application exit, missing/duplicate
completion and capture overrun fail. Existing abort and process cleanup paths
are retained. Offline actual-guard tests and capture-bound checks pass; target
staging verifies exact hashes. Both one-shots are now consumed.

## Failed physical result

One right +5 rpm request was observed. The wrapper canceled on CO_CANsend
Permission denied; four such lines are preserved. Application cancellation
reported zero-download timeout, second-zero timeout and
watchdog_retained=1000_or_unverified. No zero-target request after motion appears
in the target capture. Final zero speed, disabled state and temporary setting
restoration are unverified. Application/capture processes exited and JCAN
confirmed shutdown; no retry was made.

Target capture has 147 frames: 145 normal frames and two controller error frames,
the first 976.562 ms after the nonzero request, with raw TX error-counter bytes
96 and 128. Postflight reports ERROR-PASSIVE, one error-warning and one
error-passive event, and no bus-off. Zero instantaneous/ordinary error counters
do not override these observations. This is not a verified bus-off test.

The full JCAN archive has 6530 frames, including all 145 target normal frames
matched in order and 6385 extra identical TPDO1 payloads 2714274400002e00.
The coordinator's live count of 6288 preceded final drain. JCAN timestamps are
zero; repeated-frame timing and exact stopping latency cannot be established.

The operator confirms disconnection, reconnection a few seconds later, expected
stopping, no restart and no abnormality. This does not replace failed protocol
cleanup. The operator then explicitly reports the drive remains powered.
All hardware operations were stopped and drive power removal requested. The
operator subsequently confirmed "已经断电". Keep the drive powered off while
offline failure review proceeds; no recovery CAN request or physical retry has run.

Source inspection shows the qualification transmit gate can itself return
EACCES for absent/mismatched authorization. The logs do not identify the rejected
frame or prove an OS privilege problem. Increasing privilege or weakening that
gate is not an established remedy. The external-loss path also treats observed
CAN errors as fatal. Offline reproduction and failure-path review remain needed.

Run analyze_failure.py in this directory to reproduce the offline summary.
See failure_analysis.json and operator_observation.json. No C++ or physical
runner change was made after the consumed failure.

## Offline rejection-frame reproduction

A host-only CMake harness links the existing vcan fixture with a linker wrapper
around the real qualification transmit gate. The wrapper logs only rejected
frames and preserves return value/errno; it cannot grant authorization or send
an additional frame. The full isolated fixture exits zero with 18142 monitored
frames, prohibited=0 and failures=0, while rejected-send records include
601#806C600200000405 at external-loss scenarios. This is the SDO timeout Abort
for 0x606C:02. The pinned upstream timeout path enters ABORT and calls CO_CANsend;
the qualification gate permits only its exact pending request, so this Abort
is rejected. The same mechanism explains an EACCES diagnostic without requiring
missing OS privileges, although the four original hardware logs lack exact
rejected-frame identities and timestamps.

The existing external-loss test assertions can pass with these rejected attempts;
the test logger counts diagnostics but does not expose them to the physical
wrapper, which cancels on such a diagnostic. This is a coverage gap. Separately,
the physical controller error-warning/error-passive observations remain a
failure path even if the SDO Abort diagnostic is corrected. No policy was
relaxed, no pending CAN queue behavior was assumed safe, and no production
code, transmit gate or physical artifact was changed during diagnosis.

The initial diagnostic-harness build omitted strict C++ mode and failed on the
GNU linux macro; setting CMAKE_CXX_EXTENSIONS OFF matches the existing project
and the harness then builds. Both logs remain under offline_repro/. This is a
harness setup failure, not an additional product or hardware failure.

Required next work is a regression that checks denied-send attempts, bounded
SDO timeout cancellation without unintended Abort traffic, and a reviewed
zero-only recovery path for the observed controller-error case. Do not simply
suppress the logs, broaden the transmit allowlist, or retry the physical run.

Drive-only power interruption is blocked until this failure is resolved and its
recovery/site safety are verified. Fault/bus-off injection, emergency/brake
applicability, soak criteria and final Phase 6 acceptance remain separate work.
