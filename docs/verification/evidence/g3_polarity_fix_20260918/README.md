# G3 right-axis vehicle binding correction — 2026-09-18

**Correction and new raised dual-forward validation accepted.** Artifact9b336936,
full SHA2569b3369368e868d22208c7e2b468d2a3538f0d8303e10d41b937f10980ab1d8b2.
Operator confirms both wheels vehicle-forward, stops normal, no sound, drive OFF.
[Dual-forward acceptance](dual_positive_a1/README.md) applies the existing approved
brief-stop-transient rule while preserving original exit1/feedback failure flags.

The old dual_positive_a1 on5f44146c used right_sign=+1 and physically drove the right
wheel backward. Reuse RuntimeConfig right_sign=-1 for command and feedback binding;
left stays+1. HIL MotionGate, Trace envelope/metadata and independent auditor use
the same mapping and preserve all raw bytes. Logical(5,5) becomes wire(5,-5).
Physical motion on can0 now requires explicit --right-sign; invalid/duplicate
binding is rejected before device open. No persistent drive setting, domain-policy
rewrite, dependency, interface configuration or transient online-filter change.

## Actual software validation

- Full Debug50/50 (436.55s) and ASan/UBSan50/50 (440.85s) pass.
- Focused4 gate/trace/help tests and both20-scenario G3 polarity matrices pass.
  Independent raw vectors cover all modes, invalid bindings/extremes, wrong
  feedback signs and metadata tampering. Initial preimplementation failure is
  retained in red-test.log.
- Scoped clang-tidy22.1.8 completed; only existing platform/linux/error.hpp
  Result::value unchecked-optional advisory at97/106/115. Format/diff checks pass.
- Locked offline Docker cross build against validated actual target sysroot;
  both HIL/diagnostics ELF audits pass.123 compiler dependency inputs match the
  frozen source snapshot and current source (compiled-inputs.json).
- Legacy old-header capture replay passes1200 frames without changing its
  original physical-direction failure. Target identity/hash plus four device-free
  smoke checks pass; new unique development staging directory is recorded in
  target-smoke.json. No production deployment.

Build commands and outputs are retained in cross-build.py, cross.log, metadata,
XML/logs and ELF audits. Snapshot out/g3-polarity-v1-cross-source and output
out/build/cross/g3-polarity-v1-runtime preserve the old artifacts. before-hashes.json
records the pre-task dirty state; unrelated preexisting workspace changes remain.

## New physical attempts

| Attempt | Result | Evidence |
| --- | --- | --- |
| zero_a1 | FAILED/INCOMPLETE; operator forgot drive power | One matched SDO request, no writes/NMT/RPDO or enable |
| zero_a2 | INCOMPLETE; duplicate CH6 withdrew enable at13.640s |2314 matching frames, all targets/feedback zero, complete restoration, OFF confirmed |
| zero_a3 | PASS20s; new explicit ZERO_ENABLED console cue |3230 matching frames,2002 zero RPDOs,1689 enabled samples,36 writes/full restoration, OFF confirmed |
| dual_positive_a1 | PASS under approved transient rule; both physically forward |2.950s,1433 matching frames,357ms stable stop,36 writes/full restoration, no restart, OFF confirmed |

Each attempt used fresh local START, independent JCAN receive/ACK and target
candump, host lease and one-shot directories. A1/A2 remain failed/incomplete;
A3 runner-only visible-cue fix did not alter the artifact. All executed runners
are consumed. A single right raw+2.5rpm/logical-2.5rpm stop sample is retained and
accepted separately; original online failure semantics are not relabeled.

Remaining G3 dual-negative/turn/differential/protection cases, ground/loaded
operation and production acceptance are outside this result. Historical signed
single-wheel acceptance does not substitute for remaining vehicle-coordinate
checks. Latest OFF follows corrected dual-forward, not future powered readiness.
