# G3.1 signed-motion software preparation — 2026-09-18

Software implementation and offline qualification are complete. Physical G3.2/G3.3
remain unperformed. No deployment, target smoke, CAN-interface change or physical
motion occurred; preparation.json is explicitly authorized=false. See the
[reviewable trial sheet](../../../plans/G3_RAISED_MOTION.md).

## Change and scope

Eight new modes retain the real SBUS differential mixer: left/right negative,
dual positive/negative, opposite-sign left/right turns, and positive differential
left/right. Per-wheel targets stay within +/-5rpm; G3 hard motion duration is
3000ms (automatic cutoff2950ms). The original positive single-wheel options retain
their existing bounds. Zero/SDO restrictions, authority generations and core drive
policy remain unchanged. No dependency, thread or production entry point is added.

Signed16-bit decoding is checked at the send boundary. Candidate/profile rejection
now occurs in the sole ControlLoop owner before submission through a pure optional
static-lifetime predicate; default nullptr preserves non-qualification behavior.
A G3 reverse-input test initially exposed the old failure path: send-wrapper denial
became a terminal transport error, consuming the normal zero-stop opportunity.
The new pre-submit rejection stops through the existing bounded shutdown path;
the syscall gate remains the independent final check, with no transport retry.

All selected axes require two fresh simultaneous signed motion samples beyond the
+/-2rpm band; each received frame is checked, including startup/stop. Unselected
axes remain within +/-2rpm. New modes do not inherit deferred stop-feedback review.
Normal stop still needs a150ms standstill hold plus a fresh frame within1s, followed
by zero/Disable Voltage and verified volatile restoration. Input transitions must
match the selected profile; manual stick coordination is not established by PTY
step vectors and must be reviewed before each physical trial.

## Actual verification

- Full Debug Control HIL suite:49/49, no skips (debug.log/xml).
- Final G3 audit refinement:1/1 CTest,19 virtual scenarios (g3-final-debug.log/xml).
- ASan/UBSan first full run:47/49. Concurrent build/test execution coincided with
  feedback/decision timeout protection in legacy late-authorization and G3 cases;
  shutdown/restoration completed, and no sanitizer memory report was emitted.
  Both failed tests subsequently passed unchanged safety thresholds in a sequential
  low-contention2/2 run (sanitizer-retest.log/xml). Original failure logs retained.
  Together the final runs cover all49 tests; this is not a claim of a clean first run.
- Scoped clang-tidy22.1.8: seven affected/including translation units pass. Five
  advisory occurrences in the existing Result::value helpers are retained (three
  distinct unchecked-optional-access locations); no suppression or unrelated change.
- Default non-HIL ControlLoop/RuntimeSession compatibility:2/2 managed-vcan tests
  pass after rebuilding the runtime configuration (runtime-default.log/xml).
- Root clang-format checks, Python syntax and git diff --check pass.
- Locked offline Docker aarch64 build and real Ubuntu22.04.5 target sysroot pass;
  all123 compiler dependency inputs match the frozen source (compiled-inputs.json).
- Both qualification ELFs pass interpreter, dependencies, symbol-version and no-RPATH
  checks. The initial general Phase3 probe auditor requested clock_nanosleep, a
  non-applicable qualification symbol; its original log is retained. The existing
  qualification-specific ELF auditor passes (elf.log).

The G3 virtual matrix covers every signed mode, deadline, X1, SBUS flags/silence,
SIGTERM, right-axis status fault, wrong sign, overspeed, reverse input and missing
left/right motion feedback. It asserts zero after inhibition, no restart and full
restoration. Normal cases additionally audit the independent peer capture and
reject changed mode/targets/feedback/restart/capture order/restoration. Final trace
and capture auditing lives in scripts/test/analyze_control_hil_trace.py; the trial
sheet documents its normal-trial CLI. Fault exit1 still needs scenario-specific
causality; the normal capture audit must not be used as blanket fault acceptance.

Commands (repository root; vcan tests require isolated namespace permissions):

    cmake --build out/build/p103-control-hil -j 4
    ctest --test-dir out/build/p103-control-hil --output-on-failure
    cmake --build out/build/p103-control-hil-san -j 4
    ctest --test-dir out/build/p103-control-hil-san --output-on-failure
    ctest --test-dir out/build/p103-control-hil-san -R '^control_hil_(motion|g3)_vcan_managed$' --output-on-failure
    python3 docs/verification/evidence/g3_software_preparation_20260918/cross-build.py
    scripts/build/audit_qualification_elf.sh out/build/cross/g3-motion-v1-runtime/tools/control_hil/robot-control-hil sysroots/rk3588-ubuntu2204

Cross reproduction needs a fresh snapshot destination; never overwrite the original.
Metadata records the dirty source snapshot based on a2338896, not a new committed
revision or remote-CI pass. Compiled source identity is verified independently;
final offline audit scripts were refined after that snapshot and their current
hashes are separately recorded in source-hashes.json.

## Artifacts and remaining gates

Control HIL: out/build/cross/g3-motion-v1-runtime/tools/control_hil/robot-control-hil
SHA256:5f44146c71747eb93cdf30c8ae675b3c9fb48566c7951c53daa96dbed627c14a.
Diagnostics: out/build/cross/g3-motion-v1-runtime/tools/control_hil/robot-control-diagnostics
SHA256:93a0366526de4750a2f924881049c09e99e82e3a0031e28278e7c42f61fe67dc.

Fresh target identity/hash/smoke, a zero-target short preflight, manual coordination,
reviewed dual captures and explicit powered authorization remain required for G3
physical work. Existing G2 acceptance applies to its recorded artifact, not a
physical qualification of these new binaries. No moving endurance, loaded/ground,
P6 closure or delayed-TX kernel-repair claim is made. Earlier consumed runners,
raw evidence and failed verdicts remain unchanged. No new remote CI is claimed.
