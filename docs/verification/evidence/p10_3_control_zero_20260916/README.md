# P10.3 actual zero-only ControlLoop preparation — 2026-09-16

**Physical attempt1 FAILED; consumed. Power-off not confirmed. P10.3 OPEN.**

This directory records the new isolated zero-only HIL artifact, offline checks,
fresh staging and the consumed first physical attempt. It does not authorize future
reuse of a consumed marker. The previous TPDO2 prerequisite remains separately
archived under p10_3_hil_20260916.

## Bounds and operator sequence

- Target robot-dev, machine-id6923ab3301fb4a8d816759b04ec6bf0a, can0/node1,
  Classical500000; original raised-wheel ZLAC8015D/SBUS wiring.
- UART by-id serial586D017868, calibrated steering200/1000/1800 and
  throttle200/993/1800, unreversed, channels0/2/5/6,1000008E2.
- Actual ControlLoop may enable both axes **only with zero targets**. Independent
  syscall gate rejects any nonzero target. No physical movement trial, reset,
  persistent parameter write, interface change or cable-loss stimulus.
- New powered readiness must confirm neutral sticks, CH6 released, raised wheels,
  physical emergency stop and operator present. After application CONTROL_READY,
  press CH6 once then release, keeping sticks neutral throughout the20s window.
  No precise chat timing or manual wheel order is needed for this zero-only stage.
  Missing the fresh button edge produces a failed trial, not automatic replay.
- Setup and cleanup each have10s budgets; outer runner allows45s before SIGTERM
  and12s cleanup grace. JCAN silent capture is bounded70s and10000frames. Target
  candump begins first and remains through1s after application exit. Capture
  failure requests abort; inspect evidence before any new trial. Final operator
  no-motion/no-abnormal-sound and power-OFF confirmation is required separately.

## Reproducible verification

- Configure HIL: cmake -S . -B out/build/p103-control-hil -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DROBOT_CONTROL_BUILD_CONTROL_HIL=ON; build then ctest.
- Debug39/39 and Clang ASan/UBSan39/39, no skips; local ASan detect_leaks=0.
- Original runtime37/37, default Release35/35, P6 qualification78/78, no skips.
- Final HIL scoped6/6, sanitizer4/4. Bootstrap29 scenarios; actual executable five
  vcan/PTY cases. The Python peer models Quick Stop as requiring Disable Voltage.
- The actual final-send wrapper rejects all tested nonzero target bytes, invalid
  controlwords/widths/CAN flags. Physical-capture oracle is checked with virtual
  records and deliberate target/restoration tampering.
- Scoped clang-tidy uses clang-analyzer/bugprone/performance/portability, preserves
  project pragma-once/diagnostic enum width, and excludes unrelated header bodies.
  Local annotations explain CANopen parameter order and GNU ld-required symbols.
  Actionlint/CI selector checks pass; CI now requires the HIL virtual suite.
- cross-zero.py uses the locked Docker image and real target sysroot; metadata
  and source attestation are archived. All77 compiled units match the snapshot.
  ELF audit passes. stage-smoke-v2.log records current remote hashes and pure tests;
  stage-smoke.log retains the earlier never-run staging record.
- HIL Release and mixed-write-mode configurations reject before building. An
  initial assertion failed only because CMake wrapped its diagnostic over lines;
  whitespace-normalized checks verify both failures.

## CI-discovered UART race and replacement artifact

CI35091917436/0ee8a44 failed its sanitizer whole-executable case after safe
revocation/cleanup. Local diagnostic reproduction identifies Source discontinuity
with no cycle miss. The Reader previously classified a fresh fragment arriving
between read and FIONREAD as backlog. The added syscall-boundary PTY test fails
deterministically before the fix and passes afterward. Reader now drains fresh
fragments without waiting within its existing256-byte batch budget. A full budget
still rejects/flushes the whole batch, and service-gap/partial expiry, error
handling and fresh authorization rules are preserved. No physical thresholds
were relaxed, and this is not treated as a sanitizer memory error.

Ten consecutive sanitizer executable trials and ten PTY checks pass after the
fix. Final Debug39/39, sanitizer39/39, runtime37/37, Release35/35 and P678/78 pass
again. Static checks and the new locked cross/ELF/source comparison pass. Current
artifact f60685c2399fb45a60b288e11e3a3dce82194930b146d42d934679ab7e88c878 is staged
under p103-zero-f60685c2-20260916. bd98635b was never physically executed and its
remote authorization is explicitly false (retire-v1.log). Historical failed CI,
reproduction logs and older source attestations remain preserved. v2 metadata
identifies the replacement compiled source snapshot; remote CI is checked on the
corrected source commit separately.

## Preserved failures and execution state

The initial sandbox vcan check skipped; its explicitly permitted namespace run
reproduced partial-setup cleanup failures. The next run exposed inconsistent
TPDO-nonzero/SDO-zero cleanup; the fixed implementation refuses restoration in
that contradiction. Original failed logs remain failed. Initial static invocation
had no configured checks; the broad follow-up found project/header conventions
and local issues. Scoped final checks passed after documenting intended API
ordering, GNU symbols and explicit nonblocking log flushes. Initial Docker access
was denied in the sandbox; the approved locked-image build passed without image
updates. None of these failures caused a physical retry.


## Physical attempt1 disposition

FAILED/operator-disturbed; one-shot runner consumed. Operator reports accidental
right-wheel contact. All1341 RPDO targets were zero; measured right/high-half
speed0.2 then0.5rpm triggered inhibition.1340 cycles included916 enabled samples.
Cleanup wrote zero targets and Disable Voltage but could not verify restoration
against recent nonzero TPDO feedback. Volatile mappings/watchdog/heartbeat remain
unrestored. Last confirmed power state ON; subsequent power-off is not confirmed.
All2138 target frames match the first2138 JCAN frames; eight extra JCAN frames are
passive tail capture. See zero-failure-analysis.json and preserved raw captures.

A separate retry2 is staged with the same binary and unchanged limits, authorized
by the operator repeat request. It has NOT RUN and awaits explicit power OFF then
ON confirmation, untouched raised wheels, neutral sticks, CH6 released and an
available emergency stop. P10.3 remains OPEN.

analyze-zero.py checks identical target/JCAN frames, correlated SDOs, exact36
volatile writes, zero targets/speeds, zero-enable feedback, NMT order and mapping
restoration. It does not invent operator confirmation. Logs/metadata are compressed;
SHA256SUMS covers the current directory excluding itself and Python caches.
