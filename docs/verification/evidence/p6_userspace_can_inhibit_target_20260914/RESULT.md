# Userspace CAN inhibitor target attempt — 2026-09-14

**INVALID: NO PHYSICAL LOSS INJECTED; SAFE POSTFLIGHT.**

The target and silent JCAN captures match all 597 normal frames. The application
sent one bounded right-wheel +5 rpm target; target TPDO evidence contains 163
nonzero right-wheel samples and no nonzero left-wheel sample. The operator saw
the disconnect prompt but did not remove the RK3588 CAN branch before the
attempt ended.

No CAN error frame or communication loss occurred. After the existing 8 s loss
window expired, the application reported 'qualification_expected_external_loss_absent'
and explicitly requested the helper to set can0 down. The helper did so and
the target ended with can0 DOWN/STOPPED, no control process, both wheels
stopped, drive power off and the site safe.

The runner classified target candump exit as a failure even though its exact
diagnostic was 'can0: interface down'. That is a harness defect exposed by the
new mitigation, not evidence that physical delayed-TX suppression passed or
failed. This one-shot runner is consumed and must not be retried.

An improved runner is prepared separately in
../p6_userspace_can_inhibit_retest_20260914/. It pauses after passive capture
readiness until the operator confirms a hand is on the connector, accepts the
expected capture exit after interface inhibition, requires timely interface
down before the no-loss timeout, and watches the silent JCAN stream for any host
request more than 250 ms after interface-down observation. It remains explicitly
unauthorized.
