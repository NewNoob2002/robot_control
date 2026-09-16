# P10.3 HIL checkpoint — 2026-09-16

**P10.3 remains OPEN.** Receive-only SBUS and the stationary TPDO2 layout
prerequisite passed; actual ControlLoop zero/enable/motion HIL remains to be
implemented and accepted. This is not full remote-control acceptance.

## Completed physical prerequisite

- Operator authorized P10.3 HIL and confirmed unchanged wiring, initially
  powered-off drive, raised wheels, emergency stop and on-site assistance.
  RK3588 machine-id and UART serial586D017868 were checked. No-device smoke and
  10-second SBUS observation passed:1429 frames,1431 snapshots, all invalid/zero
  without an enable edge, final shutdown invalid. Observer has no CAN linkage.
- Added Debug-only/default-OFF qualification operation --runtime-diagnostics,
  fixed two-second observation. It requires exact mode3, both targets/speeds/fault
  zero, non-enabled status, original TPDO1 and unused TPDO2 descriptors/count/timer.
  Unknown baseline fails before writes. No controlword, target, RPDO, mode or
  EEPROM write occurs. The existing RK3588 qualification owner performs correlated
  one-at-a-time SDO transactions, never a JCAN/Python primary sequencer.
- It temporarily enables500ms heartbeat, enters Pre-operational, disables/maps/
  enables TPDO2 as6061:00/8+603F:00/32 with event timer100, enters Operational,
  observes stationary disabled feedback, returns Pre-operational and restores
  the exact baseline. Generation changes forbid restoration into a different
  boot; cleanup failure stays failed and requires operator power-off.
- Artifact SHA256:
  757c232db0a391f75fdf3d84fb67290721bfb8b3d8e00a3d723316639c981f5a.
  A new nonproduction directory and one-shot marker isolate this trial.
  After operator readiness, one driver trial ran at18:35:50+08:00.
  Target candump and silent JCAN207F346D5650 contain **252 identical frames**:
  73 correlated SDO pairs,15 exact volatile writes, and NMT Pre-operational →
  Operational → Pre-operational. There are45 DLC5 TPDO2 frames, all03 00 00 00 00,
  spanning2200.379ms with49.907–50.082ms intervals; the application's two-second
  window counts41. All TPDO1/SDO speeds remain zero and status stays non-enabled.
  TPDO2 descriptors/count/timer and heartbeat0 are restored. Application exits0
  in3872.597ms; CAN error/drop deltas are zero. Operator confirms no motion or
  abnormal sound and final drive power OFF. Software left can0 as found.

## Verification and preserved failures

Host Debug/qualification78/78 and Clang ASan/UBSan78/78 pass without skips;
local ASan uses detect_leaks=0. Final focused4/4, scoped clang-tidy, CI selector,
actionlint, locked-container cross and real-sysroot ELF audit pass. Cross snapshot
matches final compiled sources. Nineteen virtual scenarios cover foreign/active
baseline, missing/wrong/fault diagnostics, nonzero speed, SIGTERM, boot change,
each applied setup write with lost ACK and restore ACK loss. Existing P6 tests
are retained. CI requires the new managed-vcan test; remote CI is checked for
the pushed commit separately.

The first independent capture window expired during separate approval/dispatch
steps. The target freshness gate rejected launch before invocation: no driver
trial marker or SDO/NMT/RPDO. That preparation remains FAILED/driver-NOT-STARTED.
A new capture continuously orchestrated the still-unconsumed driver trial after
JCAN readiness. There was no retry of a physical SDO operation.

Initial virtual compilation lacked the new method. The first executable test
wrongly expected restored baseline after deliberately losing a restore ACK;
the corrected oracle preserves restore-failed state. Initial static findings
and successful reruns are retained. An analyzer parsing typo was corrected
without repeating hardware. These failures are not rewritten as passes.

## Remaining gates

- Compose current-generation RPDO/TPDO readbacks with one RuntimeSession and the
  actual ControlLoop. This standalone capture is not reusable proof for a later
  session. Prepare a bounded full-chain zero-target entry, including send errors,
  stop/restore order and X1 observation.
- P6 physical quick-stop recovery uses Disable Voltage after verified zero.
  A virtual peer accepting Shutdown does not qualify that physical shortcut.
- Then prepare positive single-wheel low-speed bounds and operator-visible phases
  for signs, SBUS loss/failsafe, nonneutral recovery, emergency stop and SIGTERM.
  Negative/simultaneous-wheel motion and CAN loss need their separate scope.
  Consumed capture markers are not permits. Production, loaded operation,
  P6 soak and kernel delayed-TX repair remain unaccepted.

[Raw evidence, scripts and hashes](evidence/p10_3_hil_20260916/README.md).
