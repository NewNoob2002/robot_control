# Approved standstill feedback policy — 2026-09-18

The operator confirms stationary wheels, no abnormal sound and drive power OFF,
accepts the completed three-hour CANopen/JCAN trial, and requires +/-2rpm for
near-zero measured feedback in subsequent physical tests. The accepted trial
and original strict-zero audit are both retained in
`../rk3588_can_soak_20260918/`; no raw evidence or consumed runner is changed.

## Implementation

- The current soak analyzer checks independent signed32 and packed dual signed16
  feedback in0.1rpm units against[-20,20], including all captured TPDO1 and
  velocity SDO replies. It does not average opposite signs or discard samples.
- The ZLAC qualification CLI explicitly constructs its session with20-tenths
  feedback tolerance and prints that criterion. Shared startup, stop, recovery,
  uncommanded-wheel and manual-capture preflight/cleanup checks use this band.
  Exact target/download/readback, mode, fault and mapping checks are unchanged.
- Control HIL already defaults to20. Lower-level library defaults remain0 for
  explicit protocol fixtures; physical tools inject20. Active motion must still
  meet its own criteria; near-zero feedback is not evidence of commanded motion.
- The current soak runner pins the new qualification artifact and staging path,
  so it cannot silently reuse the old strict-zero executable for a future run.
  This path is not yet deployed. Historical archived runners retain their hashes
  and remain consumed.

## Verification

Boundary tests were added before the implementation: the old Python analyzer
rejected the new +2rpm passing case. Final Python tests cover signed boundaries,
packed halves, +/-2.1rpm and invalid raw ranges, SDO replies, exact forbidden
commands, X1, capture exit, SIGHUP isolation and bounded TERM-ignoring cleanup.
Virtual-CAN scenarios cover +/-2rpm preflight/restoration, out-of-band independent
and packed feedback, invalid tolerance parameters and a nonzero target rejected
even with the tolerance enabled.

- Debug:78/78, no skips (`debug.xml`).
- ASan/UBSan:78/78, no skips (`sanitizer.xml`); ASAN_OPTIONS=detect_leaks=0.
- Focused Control HIL regression: see `control-regression.xml` and log.
- Scoped clang-tidy: exit0; existing advisory findings remain in `static.log`.
- Locked-image/real-sysroot aarch64 Debug build and ELF audit pass. See
  `cross-metadata.json`, `cross-v2.log`, `elf-audit.log` and `source-hashes.json`.
  The first cross build compiled successfully but its metadata step incorrectly
  named a host-only test binary; the corrected second build records only the
  actual aarch64 qualification executable.

New artifact SHA256:
`54f2c820395057a24a39511c8f562d224fb05c0e8d4991cefa9419f769df71b3`.
No new target deployment, device smoke, physical run or interface change is
claimed for this policy change. Local verification predates node submission;
remote CI status belongs to the submitted commit. The completed
physical soak used the original artifact; its acceptance is the explicit
operator-approved retrospective application of the feedback criterion.
