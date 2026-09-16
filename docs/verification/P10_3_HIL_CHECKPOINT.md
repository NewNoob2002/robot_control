# P10.3 HIL checkpoint — 2026-09-16

**P10.3 remains OPEN.** Receive-only SBUS and the stationary TPDO2 layout
prerequisite passed. The actual zero-only ControlLoop artifact now passes offline
verification and is staged; physical zero/enable and motion HIL remain unaccepted.
This is not full remote-control acceptance.

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
are retained. CI requires the new managed-vcan test. Remote CI run35086866920
passed for commit2489e5df6794f40b5125cf48a42114419f657bf6 on2026-09-16,
including qualification Debug/sanitizer, PDO runtime Debug/sanitizer and host
checks; the unrelated SBUS suite was not selected. This result covers the
committed prerequisite, not the subsequent uncommitted ControlLoop HIL prototype.

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

## Prepared actual zero-only ControlLoop (not physically run)

An isolated Debug/default-OFF ROBOT_CONTROL_BUILD_CONTROL_HIL build combines the
existing bounded qualification bootstrap with one Lifecycle and the actual
ControlLoop/Reader/Source/RuntimeSession. Correlated live layout readbacks qualify
only that owner generation. Bootstrap establishes zero/Ready without enabling;
fresh neutral SBUS authorization owns subsequent zero enable. GNU ld send wrapping
rejects every nonzero SDO/RPDO target, unreviewed controlword and persistent write.
The source retains real calibrated samples; rejected targets are never clamped to
make a trial appear successful. The trial is bounded to20s, with10s setup/cleanup
budgets, temporary1000ms drive watchdog, explicit runtime detachment before SDO
cleanup, zero/Disabled verification and restoration of owned volatile settings.

Partial setup cleanup no longer waits for Operational TPDOs while still in
Pre-operational. Twenty-nine virtual scenarios include all18 applied setup writes
with lost ACK, bad/absent diagnostics, motion contradictions, boot change, SIGTERM,
restore ACK loss and an attached runtime preventing SDO cleanup. A configured
status mask observes low-half X1 bit15 on every runtime event; a pulse followed by
clear cannot revive the old authority. Quick Stop recovery now sends Disable
Voltage only at verified standstill, then Shutdown after Disabled feedback. The
independent virtual peer rejects the former Shutdown shortcut.

Verification: HIL Debug39/39, ASan/UBSan39/39 (local detect_leaks=0), existing runtime
37/37, Release35/35 and P6 qualification78/78 pass without skips. Final scoped
checks6/6 and sanitizer4/4 pass. Five real-executable virtual cases cover zero
enable, nonzero rejection, X1 pulse, SIGTERM and missing diagnostics. The capture
oracle passes virtual replay and rejects target/restoration tampering. Scoped
clang-tidy, actionlint and CI routing pass. Locked cross/ELF audit and comparison
of all77 compiled translation units against the frozen snapshot pass. Target
hash verification, --help and pure ControlCycle smoke pass without CAN/UART test
execution. CI35091917436 for0ee8a44 failed the sanitizer HIL case: a fresh UART
fragment arriving between read and FIONREAD was misclassified as backlog. The
deterministic PTY reproduction fails before the Reader fix and passes afterward.
Reader now drains such arrivals without waiting within the original256-byte
budget; full-budget backlog, service-gap, partial-frame expiry and error/rearm
rules remain enforced. The unchanged physical timing limits pass ten consecutive
sanitizer whole-executable/PTY runs. All five suites above pass again after the
fix. Original CI/reproduction failures are retained, never changed to passes.
The corrected source checkpoint's remote CI is tracked separately.

Staged artifact SHA256:
f60685c2399fb45a60b288e11e3a3dce82194930b146d42d934679ab7e88c878.
Directory: /home/cat/.cache/robot-control/staging/p103-zero-f60685c2-20260916.
The earlier bd98635b artifact was never physically run; its target authorization
is now explicitly retired. The replacement one-shot runner is unconsumed and requires a fresh powered-ready statement;
current operator-confirmed disposition remains drive OFF. JCAN silent capture and
target candump precede stimulus; capture failure requests abort and every failed
trial remains failed. No movement, physical zero-enable acceptance, new emergency
stop acceptance or production readiness is claimed.

[Prepared trial, software logs and hashes](evidence/p10_3_control_zero_20260916/README.md).

## Remaining gates

- Run and accept the staged zero-only ControlLoop against fresh physical feedback,
  operator neutral/button steps, independent captures and post-trial disposition.
  The earlier TPDO2 capture is never reusable layout proof for a later session.
- Physically qualify integrated X1 and quick-stop recovery. Software regressions
  and earlier isolated P6 evidence do not close the new whole-chain HIL gates.
- Then prepare positive single-wheel low-speed bounds and operator-visible phases
  for signs, SBUS loss/failsafe, nonneutral recovery, emergency stop and SIGTERM.
  Negative/simultaneous-wheel motion and CAN loss need their separate scope.
  Consumed capture markers are not permits. Production, loaded operation,
  P6 soak and kernel delayed-TX repair remain unaccepted.

[Raw evidence, scripts and hashes](evidence/p10_3_hil_20260916/README.md).
