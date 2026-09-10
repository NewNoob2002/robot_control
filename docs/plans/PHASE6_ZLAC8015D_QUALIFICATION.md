# Phase 6 ZLAC8015D Drive Qualification Plan

Status: **IN PROGRESS — P6.5 COMPLETE; P6.6 NMT STOP, SHUTDOWN, DISABLE VOLTAGE AND QUICK STOP SLICES HIL PASS**

Current checkpoint and accepted manual TPDO result:
[Phase 6 checkpoint](../verification/PHASE6_CHECKPOINT.md). Historical evidence
paths below are resolved by the [evidence index](../verification/evidence/README.md).
Commit/push and remote CI precede any remaining physical tests.


Plan date: 2026-09-04

## Objective

Qualify one identified ZLAC8015D V4 dual-axis drive for later Linux motion
control using the Phase 5 CANopen owner and evidence chain. Phase 6 turns
manufacturer claims and legacy observations into current-repository evidence
for CiA402 state transitions, velocity command/feedback semantics, bounded
stopping, communication loss, and applicable fault behavior.

Phase 6 is a drive-qualification phase, not the production motion daemon. Its
only motion-capable artifact is separately built, Debug-only, default OFF, and
limited by an exact transmit allowlist and non-renewable bounds.

## Reviewed decisions

- Reuse the existing CANopen lifecycle, immutable observations, CiA402 domain
  decoder and transition tracker, safety manager, and monotonic timing. Do not
  create another CANopen transport, CiA402 state machine, or safety policy.
- Keep normal Debug and Release artifacts deny-by-default and non-transmitting.
  The Phase 6 artifact remains separate from the Phase 5 read-only tool.
- Use independent targets `0x60FF:01/02` first. Do not use packed target
  `0x60FF:03` until access, order, sign, and scale are verified. Initial
  qualification writes the independent targets through exact whitelisted SDO
  downloads; no RPDO mapping or generic RPDO producer is needed in Phase 6.
- Preserve neutral low/high-half names for `0x6041` and packed feedback until
  one-axis-at-a-time hardware evidence proves physical mapping.
- Advance CiA402 transitions only after a newer matching status observation;
  fixed delays alone never prove a transition.
- Prefer volatile changes. No `0x2010` EEPROM save, node-ID/bit-rate change,
  motor tuning, brake-resistor change, LSS, node reset, or persistent drive
  configuration belongs to the default Phase 6 path.
- Every physical CAN write, target deployment, interface state change, or
  motion session requires current explicit authorization and a recorded safety
  preflight. Discovery, a profile, or earlier authorization is not permission.

## Entry baseline

Phase 5 provides the pinned CANopen stack, single-owner Linux lifecycle,
immutable CANopen observations, process-wide default-deny transmit boundary,
isolated read-only commissioning path, and host/`vcan`/RK3588/JCAN evidence.
The pure domain already provides CiA402 decoding, neutral dual-half status,
transition tracking, command arbitration, and safety decisions. These are not
yet connected to a ZLAC write executor.

Historical Phase 6A/6B and M4 records summarized in
`docs/ZLAC8015D_CANOPEN_NOTES.md` are reference evidence only. Records whose
raw artifacts and source revision are outside this repository cannot close a
current Linux Phase 6 gate.

## Scope boundaries

In scope:

- reviewed ZLAC encoders/decoders and typed volatile operations;
- exact one-frame authorization at the existing upstream transmit boundary;
- zero-target CiA402 and NMT qualification;
- one-axis, low-speed, one-shot unloaded motion qualification;
- applicable stop, communication-loss, and non-destructive fault behavior;
- host, managed-`vcan`, sanitizer, static, cross, RK3588, and independent JCAN
  evidence with verified cleanup.

Out of scope:

- the complete SBUS runtime pipeline or M4 Bench capability;
- ROS2, production scheduling, systemd packaging, or release deployment;
- loaded operation, autonomous motion, position mode, or torque mode;
- arbitrary CAN/SDO access, broadcast NMT, or a generic CANopen console;
- EEPROM persistence, node-ID/bit-rate changes, motor/control-loop tuning, or
  manufacturer-unsupported fault injection;
- hard-real-time, production safety, or certification claims.

## Safety envelope for active HIL

Before a motion-capable run, record the exact RK3588, drive identity/firmware,
JCAN serial/channel, source commit, ELF SHA-256, CAN bitrate, node ID, power
state, and all values that may be changed. Confirm unloaded/clear wheels, one
named operator, and an accessible independent power-disconnect or physical
stop. Explicitly declare whether emergency inputs, mechanical brakes, and the
communication watchdog are present and required; unknown never means safe.

The first-motion envelope is one axis, the other target exactly zero, absolute
target no greater than 10 rpm, and duration no greater than three seconds. One
authorization generation permits one nonzero interval and cannot be extended
by repeated input, cycles, retries, or timestamp wrap. Expiry, stale feedback,
EMCY, bus error, link loss, termination, or any precondition mismatch clears
pending nonzero content and revokes the generation.

Normal cleanup with usable CAN is: command and verify zero, command and verify
CiA402 Shutdown (`0x0006`), enter and verify NMT Pre-operational, restore and
upload every temporary value, stop test processes, and confirm no residual
periodic transmission. If CAN is unusable, remove physical power and do not
claim a wire-level stop; restore/read back only after controlled recovery.

## Delivery slices

### P6.1 — Contract, fixture, and read-only baseline

Current status: the requirement matrix, exact read manifest, evidence layout,
and safety preflights are recorded in
`docs/verification/P6_1_CONTRACT_FIXTURE_READ_ONLY_BASELINE.md` and
the two dated P6.1 evidence directories. The first authorized attempt completed
positions 1 through 96 and stopped at a position-97 timeout. A separately
authorized continuation retried `0x2000:00` once, succeeded, and completed
positions 98 through 119 once each with exact RK3588 raw capture and cleanup.
All 119 planned JCAN values are present, and the operator explicitly accepts
the first command-session RK3588 capture. Actual-value review retained the
`0x1018:00=5` versus manual-defined `00/01/02` contradiction without
assuming undocumented subindices. P6.1 is complete.

Create the requirement matrix and evidence layout. Inventory the exact fixture
and read every object needed for eligibility and restoration without changing
drive state. Read-only SDO traffic remains an active CAN transmission and
requires its own authorization and preflight. Resolve optional safety devices
as present or not applicable.

Acceptance:

- inventory records raw value, width, source, intended use, and classification;
- current evidence covers identity, firmware, PDO mappings, `0x200F`, `0x2000`,
  `0x2007`, `0x2008`, `0x6041`, `0x603F`, `0x6061`, `0x606C`, `0x605A`, and
  applicable emergency/brake inputs;
- every future write has an exact rollback value and no drive-state-changing
  operation ran.

### P6.2 — Pure ZLAC semantics

Implement only the typed encoders/decoders required by this plan: dual status
and fault values, mode display, independent and packed velocity feedback,
independent targets, and reviewed transition controlwords. Retain raw/vendor
bits and reject malformed widths, ranges, and unresolved mappings.

Acceptance:

- host tests cover documented values, signed boundaries, little-endian layout,
  unknown status, independent halves, and malformed widths;
- physical axis names remain unavailable until evidence resolves the mapping;
- no socket, hardware dependency, or transmit authorization is introduced.

Current status: P6.2 adds pure `domain/drive` ZLAC8015D codecs for the exact
status, fault, mode, velocity, target, and transition-controlword semantics
above. Host Debug/Release, sanitizer, LLVM, and RK3588 Debug cross checks pass.
No socket, CANopen transport, hardware access, or transmit authority is
present. Physical axis naming remains unavailable pending P6.5 evidence. P6.2
is complete. The deferred RK3588 Release cross check passed on the clean
attested review snapshot on 2026-09-10; see
`docs/verification/P6_7_CLOSURE_BASELINE.md` for exact provenance.

### P6.3 — Bounded qualification executor

Add a separately configured Debug-only artifact around the sole CANopen owner.
Extend the transmit boundary only with exact typed operations needed here:
reviewed SDO downloads, node-1 NMT Start/Stopped/Pre-operational, fixed
controlwords, and bounded independent target SDO downloads. Authorization is
one-frame or one non-renewable one-shot sequence.

Acceptance:

- managed `vcan` proves allowed frames and rejects adjacent object fields,
  widths, values, nodes, COB-IDs, broadcasts, RPDOs, retries, and stale calls;
- timeout, replay/late response, signal, link loss, partial sequence, and
  failed readback enter cleanup/inhibit;
- normal and Phase 5 commissioning artifacts retain their transmit policies;
- arbitrary SDO, raw-frame, EEPROM, reset, and periodic-transmit APIs do not
  exist.

Current status: P6.3 adds the isolated Debug-only, default-OFF qualification
artifact and exact one-frame transmit gate. Host, managed-vcan, sanitizer,
LLVM, default/P5.6 isolation, and locked-image RK3588 Debug cross checks pass.
The managed-vcan suite observed 110 allowed frames, zero prohibited frames,
and zero failures across success, timeout/late response, partial readback,
feedback staleness, stale heartbeat, signal, and link-loss scenarios. No
physical CAN, deployment, drive write, or motion occurred. P6.3 is complete.
P6.4 completion is recorded below; every later hardware operation still
requires fresh authorization.

### P6.4 — Zero-target CiA402 qualification

On the authorized unloaded fixture, enter NMT Operational, set velocity mode
`0x6060:00 = 3`, require `0x6061:00 = 3`, keep both targets zero, and qualify
`0x0006 -> 0x0007 -> 0x000F`. Each step requires a newer matching state for
both neutral status halves within a monotonic deadline.

Acceptance:

- RK3588 and independent JCAN captures agree on requests, responses,
  heartbeat, TPDOs, timing tolerance, and absence of unexpected traffic;
- no motion is observed and feedback remains within recorded zero tolerance;
- mismatch, stale status, timeout, EMCY, and termination inhibit and clean up;
- every temporary value is restored and uploaded exactly.

Current status: the 180-second operator-window executor passed host, managed
`vcan`, locked RK3588 cross-build and target deployment checks. Real-drive HIL
passed the complete zero-target transition sequence and a separate post-enable
SIGTERM cleanup run with agreeing RK3588/JCAN captures, zero target/velocity
readbacks and operator-confirmed absence of motion or abnormal brake behavior.
The managed-`vcan` matrix supplies mismatch, stale/missing status, timeout, EMCY
and cleanup-failure evidence. Physical heartbeat/TPDO loss and drive-fault
behavior remain P6.6 scope. P6.4 is complete; see
`docs/verification/P6_4_ZERO_TARGET_EXECUTOR_BASELINE.md`.

### P6.5 — One-axis first-motion semantics

Run separately authorized one-shot trials inside the fixed first-motion
envelope, one physical axis and direction at a time. Compare targets,
independent and packed feedback, analyzer traffic, and physical observation.

Acceptance:

- mapping of `0x60FF:01/02`, `0x606C:01/02`, `0x606C:03`, and `0x6041` halves
  is proven or explicitly unresolved;
- sign, direction, and scale claims retain raw traffic and measured response;
- expiry cannot renew and produces verified zero plus the approved safe state;
- no trial exceeds one axis, 10 rpm, three seconds, or one nonzero generation.

Current status: the repaired first-motion executor passed authorized 500 ms and
3000 ms node-1 `0x60FF:01=+5 rpm` trials. The longer run had matching 191-frame
RK3588/JCAN captures, first zero at 3000.994 ms, measured zero velocity by
254.256 ms after zero, complete restoration, and operator-confirmed left-wheel
counterclockwise direction from the chassis left side. The right wheel remained
stationary and the mechanical brake had no abnormal action or sound. See
`docs/verification/P6_5_FIRST_MOTION_HIL_PREPARATION.md`.
The same managed-vcan sequence and the authorized physical
`0x60FF:02=+5 rpm` 3000 ms trial now pass. RK3588 and JCAN captured the same
215 frames, the first zero followed after 3001.004 ms, `0x606C:02` reached zero
427.313 ms later, and all final values restored correctly. From the chassis
right side, the operator observed the right wheel rotating counter-clockwise, the left
wheel stationary, and no abnormal brake action or sound. P6.5 therefore maps
subindex 1 to the left wheel and subindex 2 to the right wheel for positive
targets. TPDO1 packed velocity remained zero during both physical motion tests
and stays an explicit contradiction. See
`docs/verification/P6_5_FIRST_MOTION_HIL_PREPARATION.md`.

### P6.6 — Stop, loss, and applicable fault behavior

Qualify zero command, NMT Stop, Shutdown/Disable Voltage, Quick Stop, heartbeat
and TPDO staleness, SIGTERM, and controlled interface loss/reopen. Test `0x2000`
communication loss and `0x605A` only after temporary values and the mechanical
envelope are approved.

The first P6.6 slice provides and now physically passes a same-owner, bounded
node-1 NMT Stop sequence for channel 2 at `+5 rpm` for 2000 ms. Software review
found and fixed an unsafe timeout recovery that could re-enter Operational
without confirming NMT Stopped. Host, sanitizer, managed-vcan, static,
isolation, and clean RK3588 cross evidence pass. In the final authorized HIL,
RK3588 and JCAN captured the same 185 frames, NMT Stop followed the target by
2000.879 ms, stopped heartbeat followed by 104.323 ms, measured velocity reached
zero by 715.450 ms, and cleanup restored both volatile values. The operator
observed the right wheel rotating counter-clockwise for about two seconds, the left
wheel stationary, and no abnormal brake action or sound. Remaining P6.6
stop/loss cases stay separately authorized; see
`docs/verification/P6_6_NMT_STOP_HIL_PREPARATION.md`.

The second P6.6 slice physically passes a bounded channel-2 `+5 rpm` 10000 ms
trial followed by exactly one CiA402 Shutdown. RK3588 and JCAN captured the
same 381 frames, Shutdown was acknowledged in 0.322 ms, Ready to Switch On
arrived in 31.564 ms, measured velocity was reverified zero by 323.279 ms, and
cleanup completed by 343.972 ms. The operator confirmed counter-clockwise right-wheel
rotation, a stationary left wheel, and no abnormal brake action or sound.
See `docs/verification/P6_6_SHUTDOWN_HIL_PREPARATION.md`.

The third P6.6 slice physically passes a bounded channel-2 `+5 rpm` 10000 ms
trial followed by exactly one CiA402 Disable Voltage. RK3588 and persistent
JCAN silent capture recorded the same 393 frames. Disable Voltage was
acknowledged in 0.307 ms, raw dual `0x1460` arrived in 31.754 ms, channel-2
measured velocity first read zero in 420.230 ms, and all velocity views were
zero by 422.227 ms. Cleanup restored both volatile values without Shutdown,
re-enable, or retry. The operator confirmed counter-clockwise right-wheel
rotation, a stationary left wheel, both wheels stopping after Disable Voltage,
and no abnormal brake action or sound. Quick Stop and remaining loss cases stay
separately authorized; see
`docs/verification/P6_6_DISABLE_VOLTAGE_HIL_PREPARATION.md`.

The fourth P6.6 slice physically passes a right-axis +5 rpm 2000 ms trial
followed by exactly one Quick Stop with the unchanged 0x605A=5 option. Both
observers captured the same 203 frames. The nonzero interval was 2001.018 ms;
the acknowledgment followed in 0.308 ms and raw dual 0x1407 in 39.620 ms.
All velocity views first read zero at 209.322 ms, then showed small rebounds;
cleanup reverified all velocities zero at 442.289 ms and restored both
volatile values by 612.278 ms, without terminal retry or re-enable. The
operator confirmed the expected right-wheel direction, stationary left wheel,
stopping and no abnormal brake action or sound. A preceding capture-only
preflight failure had zero CAN TX and no executor start. See
`docs/verification/P6_6_QUICK_STOP_HIL_PREPARATION.md`. Remaining feedback,
watchdog, loss and applicable fault requirements remain open.

The communication-loss executor preparation now passes host Debug and
ASan/UBSan (51/51 each), default/P5.6 isolation regression, 33 managed-vcan
communication scenarios, and a fresh RK3588 Debug qualification build.
It provides separate fixed watchdog, heartbeat-loss and TPDO-loss operations,
and attempts both zero targets even if the first fails. Physical trials remain
open. After connectivity restoration, the first watchdog setup attempt aborted
in Quick Stop Active. Following a driver power cycle, a second attempt passed
the executor, host-silence and cleanup checks with 182 agreeing captured frames.
The operator confirmed stop during host silence followed by restart, with the
other wheel stationary and normal brake behavior/sound. No renewed nonzero or
Enable Operation request preceded restart, so this is not a latched stop.
Physical closure still awaits exact timing and reconciliation of the two-frame
RX-counter discrepancy; safe traffic-resumption handling remains unqualified.
An explicitly requested repeat passed executor/cleanup checks with 166 matching
frames and no sampled velocity rebound. The operator reported both wheels
stationary, making the repeat inconclusive for dynamic stopping. Added counter
snapshots place the repeated two-frame RX discrepancy during executor operation.
The resulting software correction requires independent nonzero commanded-axis
velocity before any communication-loss stimulus, with red/green regression and
fresh host, sanitizer and RK3588 build evidence. Physical validation remains open. See
docs/verification/P6_6_COMMUNICATION_LOSS_HIL_PREPARATION.md for exact stimuli,
restoration, evidence, and the pending hardware authorization.

Acceptance:

- each event records detection and command-to-zero latency, velocity response,
  terminal CiA402/NMT state, authorization revocation, and recovery behavior;
- recovery restores observation only and never resumes motion;
- emergency inputs, brakes, fault reset, and electrical bus-off pass only when
  applicable equipment and a non-destructive stimulus exist; otherwise each
  remains a named residual, not a pass;
- any fault reset uses a fresh explicit generation and existing safety gates.

### P6.7 — Regression, evidence, and closure

Current status: the 2026-09-10 closure preparation passes fresh qualification
Debug and sanitizer suites (48/48 each), default Debug/Release (28/28 each),
P5.6 (36/36), artifact isolation, clean-source RK3588 Debug/Release and a
separate qualification cross build. Remaining physical requirements prevent
phase closure. See `docs/verification/P6_7_CLOSURE_BASELINE.md` for the
requirement dispositions, packed-feedback restriction and source provenance.

Run the applicable local and target qualification and publish one Phase 6
baseline. Preserve failures and distinguish software, simulated-bus, passive,
active zero-target, and motion evidence.

Acceptance:

- Host Debug/Release, full CTest, managed `vcan`, ASan/UBSan, changed-source
  LLVM checks, dependency checks, and locked-sysroot RK3588 Debug/Release pass;
- target checksum matches deployment and every active session has a
  `safety_preflight`, `bus_validation`, `test_result`, raw RK3588 and JCAN
  capture, plus cleanup evidence;
- Phase 4/5 regression proves isolation, bounded lifecycle, and zero normal TX;
- the baseline marks each hardware claim pass, fail, not applicable, or
  unavailable and states the residual risk accepted for closure.

## Slice order and authorization rule

Implement P6.1 through P6.7 in order. P6.1 through P6.3 do not authorize later
physical writes. P6.4, every P6.5 trial, and every P6.6 stimulus require fresh
dated authorization after review of the exact preflight. Stop on `ok=false`,
warning, mismatch, unexpected traffic, timeout, cleanup error, motion outside
the envelope, or unavailable physical stop. Never silently retry hardware.

## Completion gate

Phase 6 is complete only when the reviewed Linux artifact proves zero-target
CiA402 transitions, at least one bounded one-axis command and verified cleanup,
axis/sign/scale semantics required by the next phase, applicable stop and
communication-loss behavior, artifact isolation, and the full evidence chain.

Features absent from the declared fixture may be not applicable. A
required-but-unknown feature, unresolved mapping needed for safe motion, failed
cleanup, or missing independent stop path blocks closure. Electrical bus-off,
loaded dynamics, production brake tuning, and certification are not implied.

Corrected watchdog trial 4 (2026-09-10) verifies initial right-axis motion,
subsequent zero feedback, operator-confirmed rotation then stop, and exact
170-frame/counter reconciliation. Timing and traffic-resumption safety remain
open; physical heartbeat/TPDO-loss trials are pending. See
../verification/evidence/p6_6_20260910_watchdog_trial_4/RESULT.md.

A separately authorized manual right-wheel capture validates changing feedback
with the supplied speed-first TPDO1 mapping (1205 samples, median50.007ms).
Original mapping was restored. RX accounting and small left-speed excursions
remain open; enabled-drive watchdog timing is not qualified. See
../verification/evidence/p6_6_20260910_manual_tpdo_trial_1/RESULT.md.
