# P6 X1 emergency-input zero-motion pretest — prepared 2026-09-14

Status: **AUTHORIZED ON 2026-09-15, NOT RUN**. The target-side Python syntax check passes for the X1 runner and overnight soak script. One zero-motion X1 pretest is authorized; no CAN operation has started.

The fixture uses the latching, normally-open X1/INPUT2 contact. Historical readback reports X1 function 9, no input inversion, and X1 stop handling value 0. This trial does not change those values.

The test reuses the existing `--manual-tpdo` qualification operation. It keeps both drive halves disabled, verifies targets and all three speed views are zero, temporarily enables the reviewed heartbeat/TPDO observation, and sends no target, RPDO, or controlword. JCAN serial `207F346D5650` observes in silent mode.

The operator presses and locks X1 for about three seconds, then rotates or pulls it into its maintained reset state and observes at least five seconds with no motion. Acceptance requires both packed statusword halves to show bit 15 inactive, active, then inactive; every TPDO and sampled SDO velocity remains zero; no CAN error, EMCY, unexpected request, wheel motion, or abnormal sound occurs. The drive is then powered off and `can0` is left down.

This establishes X1 electrical semantics, status reporting, and zero-motion reset behavior. It does not establish moving stop time, stopping distance, or loaded behavior. A separate bounded moving test is considered only after this pretest passes.
