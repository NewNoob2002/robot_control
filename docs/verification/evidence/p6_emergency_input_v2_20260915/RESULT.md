# X1 zero-motion electrical/status pretest V2

Date: 2026-09-15

Classification: **PASS WITH RUNNER STATUS-HALF EXPECTATION DEFECT**.

The repaired qualification ELF accepted initial packed status `0x14001400`, completed the full 60-second disabled `--manual-tpdo` observation, restored heartbeat zero and NMT Pre-operational, and exited 0. The operator pressed and locked X1, held it for more than three seconds, reset it, observed more than five seconds with no motion, and confirmed no abnormal sound. Final drive power is off and `can0` is DOWN/STOPPED.

RK3588 and silent JCAN each captured the same 1989 frames in exact order. There were no CAN error frames, RPDOs, targets, controlwords, SDO aborts, or nonzero velocity samples. All 1206 TPDO1 velocity payloads and all sampled SDO velocities were zero.

X1 changed packed status from `0x14001400` to `0x14009400` and back. The low status half therefore observed bit 15 inactive, active, then inactive for 15784.523 ms; the high half remained `0x1400`. This is the hardware-observed X1 status behavior. It does not assign that status half to a physical motor axis.

The runner wrapper is false only because its analyzer required bit 15 on both status halves. The retained offline analyzer corrects that expectation and classifies the physical zero-motion pretest as passed. No physical retry is required. This result validates X1 electrical/status and reset behavior at zero motion; it does not establish moving stopping time or distance.
