# Moving X1 emergency-input behavior

Date: 2026-09-15

Classification: **PASS — RUNNER OPERATOR-MARKER TIMEOUT RETAINED**.

The right wheel received one packed +5 rpm target and produced nonzero TPDO
feedback while the left wheel remained zero. The operator pressed and locked
X1 during motion. Hardware status changed only on low-half bit 15. The right
wheel reached three consecutive zero samples 99.902 ms after that transition,
861.245 ms before the application's scheduled packed zero-target request. No
later nonzero speed or CAN error appeared.

RK3588 and silent JCAN each retained the same 225 frames in exact order. JCAN
submitted no data-frame command and its configuration was unchanged. The
qualification application logged successful operation 4 completion and all
owned processes and captures stopped.

The raw wrapper failed because it waited for the operator to type
`ESTOP_LOCKED` after physically pressing X1. The 3000 ms motion interval ended
before that text marker arrived, although 21 X1-active TPDO samples were already
captured. This is a runner interaction defect, not a missed physical stimulus.
No retry is required.

The operator confirms normal right-wheel stopping, a stationary left wheel, no
abnormal sound or restart, final drive power off, and a safe site. X1 remained
locked until power-off and was then reset. The earlier accepted V2 zero-target
trial supplies the powered reset/no-restart evidence. Together these results
close the fixture's X1 behavior gate for raised-wheel low-speed qualification.

This does not establish loaded stopping distance, mechanical braking, or
certified emergency-stop performance. Final state is drive power OFF, X1 reset,
and `can0` DOWN/STOPPED.
