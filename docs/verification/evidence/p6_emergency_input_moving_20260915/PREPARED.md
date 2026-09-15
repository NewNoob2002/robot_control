# Moving X1 emergency-input trial preparation

Prepared: 2026-09-15.

Status: **CONSUMED**. The physical result is recorded in [RESULT.md](RESULT.md).

The one-shot runner uses the repaired root-owned qualification ELF SHA-256
bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7.
It commands only right wheel +5 rpm for at most 3000 ms, with the left target
zero. Both RK3588 candump and JCAN serial 207F346D5650 in silent receive mode
capture the complete trial.

After both captures are ready, the operator places a hand on X1 and types
ARMED. The application starts motion once. The target runner waits for
nonzero right-wheel TPDO feedback before displaying PRESS X1 NOW. X1 remains
locked while the motion executor writes its normal zero and disabled cleanup.
The repaired disabled observer then verifies active X1 status and zero target
and speed before displaying SAFE TO RESET X1. Reset is followed by at least
five seconds of no-restart observation.

Acceptance requires right-wheel motion before X1, a stationary left wheel,
low status-half bit 15 activation, stable zero right speed before the
application's scheduled zero target, no later motion, low-half bit 15 reset,
no high-half bit 15 activation, no CAN error or unreviewed request, exact dual
capture correspondence, and operator acceptance. The result applies only to
the raised-wheel +5 rpm setup; it does not establish loaded stopping distance
or certified safety performance.

The runner is single-use and has no automatic retry. Final cleanup requires
drive power OFF and can0 DOWN.
