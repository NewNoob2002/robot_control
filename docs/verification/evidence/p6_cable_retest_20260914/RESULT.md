# Cable repair target verification — 2026-09-14

Status: TARGET SOFTWARE PASS; PHYSICAL RETEST FAIL_POST_ERROR_PHYSICAL_REQUEST.

The operator authorized target and physical retesting. Stable machine identity
matches the earlier RK3588. Final repair source hashes match; the new executable
SHA256 is `acc9f8828f657d968cb2c56a586b1f9e1f2c50250f6c1385d3e6b7439abed46f`.
It and both test binaries were copied to a new nonproduction directory recorded
in deployment_manifest.json and verified remotely. Previous runners are untouched.

- Target qualification isolated-vcan: exit 0, 18254 frames, prohibited=0, failures=0.
- Target lifecycle isolated-vcan: exit 0. Both stderr files are empty.
- Actual qualification executable with no arguments: expected usage exit 2;
  argument rejection precedes CAN lifecycle creation.
- Physical can0 was read only: ERROR-PASSIVE, tx/rx instantaneous error counters
  zero, 500000 bit/s, restart_ms=100. It was not reset or used for application TX.
- SSH sudo requires a password. Nonprivileged namespaces nevertheless worked,
  so target tests did not require operator intervention.

A bounded one-shot restore_can0.py is staged for the local sudo terminal. It
requires explicit POWER_OFF input, checks target identity and known sender
processes, records state, performs one down/up without changing bitrate or restart
policy, and verifies ERROR-ACTIVE. Failure attempts to leave the interface down;
there is no retry. Mocked success, missing power confirmation, active sender and
failed postcheck scenarios pass. The script has NOT run on hardware.

Physical motion remains gated on interface recovery and fresh operator/site
readiness. A new cable trial must preserve both possible outcomes: normal
zero-first recovery without a controller error, or explicit inhibition and
manual power-off after a latched controller error. The latter is not verified
protocol cleanup. No old consumed runner or stale READY may be reused.

## Interface recovery and physical preparation

Operator executed the one-shot restore with POWER_OFF. Retrieved records confirm ERROR-ACTIVE and unchanged bitrate/restart policy. No motor trial has started. New independent local/remote runners and a non-actuating operator timing display are prepared and remotely hash-verified; operator_ready remains false. Historical 147-frame parser regression retains both errors, strict diagnostic/outcome and capture-bound tests pass. JCAN self-test passes; sandbox USB enumeration failed, unrestricted read-only enumeration/config succeeded for serial207F346D5650 with unchanged baseline. No JCAN CAN session or TX has started. Next gate: fresh powered/raised/stationary wheels and emergency-stop/connector readiness, acknowledged in the local timing display and chat.

## Consumed physical trial

Fresh READY was verified and exactly one right +5rpm trial ran. The executor returned the expected INHIBITED_POWER_OFF_REQUIRED status, without CO_CANsend or Permission denied diagnostics. The wrapper correctly failed its post-error TX check: one read-only 0x601#406C600200000000 appeared 4360.145ms after the first controller error. There was one nonzero target in total and no second motion target. Kernel/controller queued transmission is a hypothesis requiring offline investigation, not a proven cause.

Target capture contains 320 frames including three CAN errors; all 317 ordinary frames match in order within the 16914-frame JCAN archive. The coordinator count16912 preceded final drain. Application and target capture stopped; JCAN shutdown confirms stopped. can0 postflight is ERROR-PASSIVE. Operator confirms drive powered off, both wheels stopped and site safe; normal stop after unplug, no restart after reconnect, and left wheel stationary throughout. See operator_observation.json. All one-shot markers are consumed. No retry, further CAN or re-enable is authorized by this result. See physical_analysis.json and original raw evidence.

Operator acceptance of physical stopping does not change the failed post-error request check or establish protocol cleanup. Investigation of the delayed read-only frame remains open.
