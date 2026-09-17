# F1 A3 — explicit ±1rpm zero-target feedback tolerance, PREPARED

User explicitly selected “+-1rpm” after A2. This is a prospective, Debug/default-OFF
qualification setting, not a reinterpretation of A1/A2 or motion failures.
CLI: --zero-x1-recovery --zero-feedback-tenths-rpm 10, control window60000ms.
Artifact:0d379ed8861c9c2fb06bc08502ea52cb8f6bfeaac4bdddc25515a328a094770e.

The default remains exact zero. CLI rejects negative/>10/noninteger tolerance and
rejects tolerance selection in motion/receive-only modes before device activation.
Trace, recovery observer, injected RuntimeConfig and qualification SDO/TPDO checks
use the same signed both-axis bound. Both transmitted targets remain exactly zero.
Preflight and cleanup require correlated606C:01/02/03 reads within the bound for
at least150ms from completion of the first good sample round. Outliers fail,
not silently restart the hold. Raw feedback is never rounded/clamped/rewritten.
Motion negative-feedback criteria and independent zero send gate are unchanged.

## Verification

- Test-first trace/API checks rejected the old implementation; new signed endpoints,
  opposite half, packed feedback overflow and invalid configuration cases pass.
- Virtual recovery accepts alternating±1rpm on both axes through X1/rearm/cleanup;
  1.1rpm is rejected and later good feedback cannot clear the original failure.
- Host Debug43/43 and ASan/UBSan43/43, no skips. ASAN_OPTIONS=detect_leaks=0.
- Scoped clang-tidy passes; project pragma-once convention excluded from portability
  rule. Initial swappable-parameter/style findings and compiler failures retained.
- Locked cross101 steps, ELF ABI/dependencies/version/RPATH audit pass;122 compiled
  source/project-header hashes match frozen source. No production policy change.
- The Phase3-specific ELF checker was initially selected by mistake and rejected
  missing clock_nanosleep; the existing qualification ELF checker passes.

## One-shot execution

A3 uses A2's direct visible terminal and START checklist, fresh independent capture
then target candump. New stage, new markers, no automatic retry. Phase actions are
printed once; the countdown updates in place to avoid repeated action instructions.
Expired/exit/cleanup snapshots suppress actions. Terminal interruption/stale target
progress requests abort and retains capture during bounded cleanup. All original
60s/90s/12s/120s/20000frame bounds apply. JCAN remains silent.

A2 operator confirmed OFF, visually stationary wheels and no abnormal sound;
its incomplete volatile restoration stays FAILED. A3 verifies the full current
baseline before writes, including mappings/watchdog/heartbeat/605A=5; never assumes
that power removal restored it. A3 physical readiness and acceptance are pending.
See threshold-authorization.json, safety-preflight.json and stage-smoke.log.

### A3 actual disposition — baseline mismatch

A3 executed once after local START, failed before CONTROL_READY: 2000:00 reads1000,
required0. Exactly8 SDO uploads, zero writes/NMT/RPDO,16-row complete trace. The
standstill tolerance was not reached. restore_ok1 means no new ownership to clean,
not recovery of A2 residual settings. Operator confirms no CH6/X1, wheels stationary,
no sound and OFF. Both markers consumed. Known-residual restoration is required
before another full baseline preflight; no automatic retry or acceptance claim.
