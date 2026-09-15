# Userspace CAN inhibitor physical retest — 2026-09-14

**PASS; NO PHYSICAL RETRY REQUIRED.**

The operator prepared at the target terminal before motion, entered ARMED, and
removed only the RK3588 CAN branch after DISCONNECT NOW. The right wheel stopped
normally, the left wheel remained stationary, and reconnecting the same branch
did not restart motion or produce abnormal sound. The operator entered
RECONNECTED, observed target DONE, powered the drive off, and confirmed both
wheels stopped and the site safe.

The RK3588 capture contains one controller error at 1789377758.304241. The
helper left can0 DOWN/STOPPED at 1789377758.316804, 12.563 ms later and 1.224 s
after the motion prompt. The error is the target capture's last frame; there is
no later RK3588 NMT, RPDO or SDO request.

All 171 normal RK3588 frames match the silent JCAN capture in order. JCAN then
retained 99833 post-error frames, all drive-side 0x181 retransmissions, with no
RK3588 request. The live coordinator also counted zero RK3588 requests more
than 250 ms after interface-down notification. Conservative clock-correlation
analysis retains over 24 s after interface down, covering the historical
4.360145 s delayed-request interval. JCAN configuration was unchanged.

The target wrapper passed. The outer coordinator later returned nonzero only
because the drive-side no-ACK retransmissions reached its fixed 100000-frame
bound. That bound occurred after the required delayed-TX observation interval;
it neither caused a retry nor invalidates the retained target and JCAN evidence.

This validates the userspace can0 inhibition mitigation for the authorized
raised-wheel cable-loss case. It remains a mitigation around the existing
Rockchip driver, not a kernel defect correction. can0 remains DOWN and any later
restoration still requires drive power OFF and a separate explicit procedure.
