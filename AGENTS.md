# Robot Control Middleware — Project Agent Guide

## Mission

Build a production-oriented, Linux-native low-level motion-control middleware for an RK3588 robot. The middleware owns CANopen/CiA402 drives, SBUS input, command arbitration, robot-level safety, health, diagnostics, and safe process lifecycle. ROS2 is an adapter and must not be the safety authority or the owner of the drive lifecycle.

## Current State

- P9.0's SBUS contract and P9.1's pure fixed-profile streaming parser are accepted.
  P9.1 passes host Debug/Release, ASan/UBSan, scoped static checks and the restored
  locked-container aarch64 Debug build. See
  docs/verification/P9_1_SBUS_PARSER_BASELINE.md.
- P9.2 UART/source runtime and the receive-only observer are implemented.
  See docs/verification/P9_2_SBUS_UART_BASELINE.md for software verification;
  P9.2 is CLOSED within its UART/receive-only scope, with authorized static and
  30-second target functional evidence. Source CI passed for c42e2a5
  (Actions run 35051777345). The operator confirmed inverted SBUS to module RX,
  common ground, a current 5 V module setting (switchable to 3.3 V), and 5 V
  receiver power. Signal amplitude and RX voltage tolerance were not measured.
  The first manual attempt failed at the old 4096-read limit; it remains failed.
  A byte-budget fix passes host Debug/Release/sanitizer 31/31 and locked cross.
  The second attempt records 4290 frames, operator-confirmed CH1/CH3/CH6/CH7,
  flags 0->4->12->0 and SIGTERM exit 143 within 16 ms of signal/reap observation.
  Three startup candidates were rejected before resynchronization. See
  docs/verification/evidence/p9_2_sbus_manual_20260916/RESULT.md.
  Electrical measurements and physical USB unplug are not claimed. P9.3 health/mapping/command snapshots and the Reader bridge are implemented.
  No motion authority is included; capture authorization is not a future test permit.
- P9.3 is CLOSED within receive-only input scope. Software,
  archived-event replay and an initial no-device target smoke pass. An authorized
  30-second calibration captured 4284 healthy frames: steering 200/1000/1800 and
  throttle 200/993/1800, neither reversed, with operator-confirmed forward/right
  logical signs. Final Source HIL attempt2 passes with 6434 frames, 6431 snapshots, three fresh authorizations and SIGTERM zero/invalid within 33.3009 ms. Operator confirmation is recorded. Attempt1 remains FAILED; attempt2 startup oracle correction and original report are preserved. All runners/authorizations are consumed, not future test permits. P8-R is accepted within its software/vcan scope. P10.1 is accepted within
  offline control-cycle scope; P10.2 software/vcan closure is implemented and verified;
  P10.3 remains OPEN. Initial receive-only and no-motion TPDO2 layout HIL pass:
  252 matched dual-capture frames, mode3/fault0, exact volatile restoration;
  operator confirms no motion/no abnormal sound and drive power OFF. Full
  ControlLoop zero-target retry2 is accepted:3149 matching frames,2002 zero RPDOs,
  fresh SBUS enable and exact restoration. Left and right A1 motion diagnostics
  reproduce post-stop negative feedback without negative targets; both original
  feedback criteria remain FAILED. Operators confirm normal direction/stop,
  opposite wheel stationary, no abnormal sound and drive OFF after right A1 on
  2026-09-17. All those runners are consumed. F1 zero-X1 A1 is consumed and
  FAILED/INCOMPLETE: only CH6 operated, no X1 stimulus before the60s limit.
  Dual8912 frames and exact restoration verified; operator confirms stationary
  wheels, no abnormal sound and power OFF after F1. A new trial needs reviewed
  phase coordination and fresh powered readiness. A2 direct terminal coordination
  works, but zero-target left feedback0.3/0.2/0.4rpm causes FAILED before X1;
  zero/Disable Voltage read back, volatile restoration INCOMPLETE. Operator
  confirms stationary wheels/no abnormal sound/OFF after A2. User requests
  a prospective zero-feedback tolerance of±1rpm. A3 artifact0d379ed8 has explicit
  zero-only10-tenths-rpm checks and>=150ms standstill holds; default remains0,
  old failures unchanged. Debug/sanitizer43/43, cross/static/ELF pass. Fresh A3
  readiness and full baseline readback are required; physical recovery remains pending. See
  docs/verification/P10_3_HIL_CHECKPOINT.md and
  docs/verification/P10_2_CONTROL_LOOP_BASELINE.md and
  docs/verification/P10_1_CONTROL_CYCLE_BASELINE.md.
  See docs/verification/P9_3_SBUS_SOURCE_BASELINE.md.
- P8-R adds a separate Debug-only/default-OFF CANopen runtime library, guarded
  dual-axis RPDO output and per-event feedback inhibition. Runtime sessions require
  caller-verified current-generation layout readbacks; P8's TPDO2 mapping evidence
  was software-only, with the later P10 stationary prerequisite recorded above. No SDO/NMT configuration,
  physical control, automatic fault reset or production entry point was added.
  See docs/verification/P8_R_RUNTIME_BASELINE.md. P10.1 offline policy integration is accepted. Remote SBUS control still requires
  separately authorized P10.3 HIL. P10.2 connects PTY/Source, the single control
  owner and one RuntimeSession to an independent vcan peer; no production entry
  point, physical preflight or hardware authorization is added.
- Phases 0–5 are complete. Phase 6 remains open; use
  docs/verification/PHASE6_CHECKPOINT.md for current acceptance and
  docs/verification/evidence/README.md for direct or archived evidence.
- Accepted bounded Phase 6 evidence covers zero-target transitions, independent
  wheel/sign feedback, revised stop/communication-loss cases, moving SIGTERM,
  userspace cable-loss inhibition, drive-power restoration and composite X1
  behavior. Earlier failed/invalid trials remain failed/invalid.
- On 2026-09-15 the operator deferred long-duration soak until SBUS and the
  integrated command/safety pipeline are ready. This permits subsequent component
  development, not Phase 6 closure, loaded operation or production acceptance.
  The v3 three-hour attempt failed after 309 s / five passing cycles when candump
  exited. V4 was staged but never started; its JCAN preparation failed on an
  invalid USB receive packet. The SIGHUP/session repair passes offline host and
  target tests; no successful physical soak is claimed.
- All consumed runners remain consumed. V4 authorization is retired on deferral.
  Archived authorizations are evidence only, never permission to run hardware.
  Any later soak needs a new artifact/preflight and explicit authorization.
- Keep qualification Debug-only/default-OFF. The independent can0 inhibitor is
  a userspace mitigation, not a kernel repair; it never brings an interface up
  or sends CAN. Exact rockchip_canfd worker/stop concurrency remains unresolved.
- No production daemon, full SBUS/M4 pipeline, ROS2, persistent drive changes or
  loaded operation is included. Latest explicit physical disposition is drive OFF
  after right A1 on2026-09-17; use each new trial's timestamped disposition rather
  than treating this historical OFF as a future powered-readiness confirmation.
- EasyLogger's checksum-pinned core subset remains integrated behind
  `service/logging`. CANopen dependency provenance and zero-local-patch status
  are recorded in `third_party/README.md`.
- `docs/ZLAC8015D_CANOPEN_NOTES.md` and the vendor PDFs are drive references; statements marked for hardware verification are not safety assumptions.
- The legacy STM32 project at `~/Desktop/workspace/STM32_PROJ/STM32G474_CANOPEN_Copy` is read-only architectural evidence, not a source tree to copy.
- Phase 1 build evidence and remaining target validation are recorded in
  `docs/build/PHASE1_BUILD_BASELINE.md`.
- Phase 2 behavior traceability and verification are recorded in
  `docs/verification/PHASE2_DOMAIN_BASELINE.md`.
- Phase 3 Linux adapter verification is recorded in
  `docs/verification/PHASE3_LINUX_PLATFORM_BASELINE.md`.
- Phase 4 scope and acceptance criteria are recorded in
  `docs/plans/PHASE4_SOCKETCAN_FOUNDATION.md`.
- Phase 5 scope and delivery slices are recorded in
  `docs/plans/PHASE5_CANOPEN_INTEGRATION.md`.
- Phase 6 scope, safety envelope, delivery slices, and completion gate are
  recorded in `docs/plans/PHASE6_ZLAC8015D_QUALIFICATION.md`.
- P6.1 software preparation and the remaining physical-read gate are recorded
  in `docs/verification/P6_1_CONTRACT_FIXTURE_READ_ONLY_BASELINE.md`.
- P6.2 pure protocol semantics and software evidence are recorded in
  `docs/verification/P6_2_ZLAC_PROTOCOL_SEMANTICS_BASELINE.md`.
- P6.3 bounded executor and software/vcan evidence are recorded in
  `docs/verification/P6_3_BOUNDED_QUALIFICATION_EXECUTOR_BASELINE.md`.
- P5.6 implementation and verification evidence is recorded in
  `docs/verification/P5_6_READ_ONLY_COMMISSIONING_BASELINE.md`.

## Latest P10.3 disposition (2026-09-17)

Corrected observer616e8ad3 was deployed and A5 passed zero-target X1 HIL. Fresh
source/system1→2 rearm completes via native Disabled without extra CH6 toggles;
nonneutral rejection and1.010s enabled hold verified.3406 dual-capture frames match,
all2123 RPDO targets zero,36 volatile writes/full baseline restoration verified.
Operator local OFF confirms stationary wheels/no abnormal sound/drive power OFF.
Both A5 one-shot markers consumed. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/zero_x1_a5/README.md.
A4's original combined claim remains superseded; original records are unchanged.
P10.3 remains OPEN for remaining moving/integrated fault cases. Earlier pending
corrected-observer HIL descriptions are historical checkpoints, superseded by A5.

Latest F2 A1 also passed the zero-target SBUS valid-frame-timeout recovery scenario:
8586 dual-capture frames,5721 zero RPDOs, complete restoration, fresh1→2 recovery through
quick-stop/Disable Voltage/new Disabled. Operator confirms OFF/stationary/no sound.
Flags4/12 were not observed; electrical UART silence/F5 is not claimed. Both markers
consumed. See docs/verification/evidence/p10_3_fault_recovery_20260917/zero_sbus_a1/README.md.

Latest F2 A2 passed actual handheld-transmitter loss/recovery with receiver continuously
powered: flags0→4→12→0,4010 matching frames,2542 zero RPDOs and full baseline restoration.
Operator OFF/stationary/no abnormal sound confirmed; both markers consumed. A1 is now
explicitly receiver-power interruption, not transmitter-loss evidence; originals preserved.
See docs/verification/evidence/p10_3_fault_recovery_20260917/zero_sbus_a2/README.md.
Moving scenarios and F5 remain pending; P10.3 OPEN.

Latest F3 A2 (778b0a79 --right-throttle) remains INCOMPLETE: single-throttle right
motion works, but2.95s cutoff preceded X1 by260.791ms; post-stop negative feedback
minimum-1.1rpm remains FAILED.1716 matching frames/full baseline restored; operator
OFF/no sound/left stationary confirmed. A1/A2 consumed; no automatic retry. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/motion_x1_a2/README.md.

Latest user-approved criterion: single-wheel direction/stop acceptance is CLOSED.
Stopping negative feedback is classified normal internal drive speed-estimation fluctuation
per explicit user confirmation. Subsequent HIL uses±1.5rpm (CLI15/default15) for the near-zero
feedback band, including motion startup/stop and cleanup; raw values remain unchanged.
Targets, positive-motion bounds and X1 timing still require independent acceptance.
Historical raw evidence/original failures are preserved; revised left/right A1 audit is at
 docs/verification/evidence/p10_3_fault_recovery_20260917/standstill_15_revision/README.md.
F3 A2 remains incomplete because X1 was late; P10.3 remains OPEN.

Latest F3 A3 is PASS with3652f15b and explicit±1.5rpm: X1 during1.848s right motion,
receive-to-zero32.084us, stable band verified299.740ms, no restart;1433 matching frames
and full restoration. Raw post-stop−0.5..+0.2rpm classified normal within approved band.
Operator OFF/right direction+stop normal/left stationary/no sound/X1 locked confirmed;
A3 consumed. Expected protective exit1 is independently audited, not a feedback failure.
Single-wheel acceptance CLOSED; F3 accepted; remaining F4/F5/F6 keep P10.3 OPEN. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/motion_x1_a3/README.md.

Latest F4 A1/A2/A3 remain INCOMPLETE and consumed. A1 operator used X1 instead of
transmitter OFF; A2 rejected active X1 at startup (one read/no writes/no motion).
A3 stops at2950.027ms automatic cutoff, not SBUS loss:1103 frames flags0, Source
still enabled, no X1.1435 dual frames match,748 RPDOs/full baseline restored;
operator OFF/right direction+stop normal/left stationary/no sound confirmed.
Transmitter needs a long press; recorded throttle remains forward through cutoff,
so early neutral is not the stop cause. Actual RF-off timing remains unknown.
No automatic retry/longer window. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/motion_sbus_a3/README.md.
F4/F5/F6 pending; P10.3 OPEN.

## Non-Negotiable Architecture

Dependency direction is inward toward protocol-independent control policy:

```text
app / ros2_interface / tools
        |
        v
application orchestration
        |
        v
motion + safety + drive domain
        |
        v
communication/canopen + input/sbus
        |
        v
platform/linux
        |
        v
Linux kernel APIs
```

- `platform/linux` wraps Linux mechanisms only; it contains no motion, arbitration, CiA402, or safety policy.
- `input/sbus/protocol` never includes `termios`, file-descriptor, or Linux headers.
- ROS2 callbacks publish timestamped command samples only. They never send CAN, mutate a drive, or bypass arbitration/safety.
- Only the control-cycle owner may publish the final drive command.
- CiA402 protocol state and robot-level safety state are separate state machines with an explicit adapter.
- CANopen remains operational and safe when ROS2 is absent, restarting, or stale.
- Configuration is injected at startup and validated before device activation; deployment values are not scattered constants.
- Device-tree/pinmux configuration belongs to the kernel/boot configuration, never this application.

## Online drive attachment

A drive may already be powered before RK3588 starts. Do not require historical
boot-up or ask the operator to power-cycle merely to attach. Use current online
SDO/heartbeat/TPDO evidence and the complete zero/mode/fault preflight. A later
actual boot-up still invalidates the active generation. Zero/first-motion CLI
uses the bounded online heartbeat preparation; see
`docs/verification/P6_REVIEW_ONLINE_STARTUP.md`.

## Safety Invariants

- Startup, source handover, recovery, stale input, missing feedback, CAN loss, drive fault, and shutdown produce zero motion before any enable/re-enable action.
- No non-zero command is accepted without a fresh monotonic timestamp, valid source sequence/generation, current authorization generation, valid drive feedback, and an eligible safety state.
- Switching command ownership requires a zero-hold interval and a new authorization/rearm generation. The legacy 150 ms value is the initial compatibility baseline, not an unchangeable constant.
- Fault reset is gated and never periodic or unconditional.
- Lost CANopen heartbeat/TPDO, bus-off, device disappearance, SBUS failsafe/staleness, or stale ROS2 command invalidates the affected authority immediately.
- Automatic recovery may restore communication observation, but motion reauthorization requires explicit state-machine criteria and neutral/zero confirmation.
- SIGTERM handling first inhibits new motion, then commands zero/safe drive state within a bounded deadline, then stops CANopen and releases resources. A crash cannot guarantee an application-level shutdown sequence; drive communication-loss protection is required as defense in depth.

## Time, Concurrency, and Ownership

- Represent internal time as `std::chrono::steady_clock`-based time points/durations or an equivalent injected monotonic clock. Do not use wall-clock time for validity or timeout decisions.
- Use one control-cycle owner for arbitration, safety evaluation, CiA402 intent, and final command publication.
- Share immutable, versioned snapshots between producers and the control owner. Avoid cross-thread mutable domain objects and callback-driven policy.
- Avoid one thread per module. Initial target model: CANopen event loop, SBUS reader, control cycle, and optional ROS2 executor; diagnostics are periodic work, not necessarily a thread.
- Initial control period is 10 ms (100 Hz), subject to measurement. CANopen transport deadlines and PDO processing may run at 1 ms/event-driven cadence without forcing robot policy to 1 kHz.
- Do not claim hard real-time behavior. Record wake-up latency, cycle execution time, command-to-RPDO latency, and missed deadlines before considering `SCHED_FIFO`, affinity, isolation, or PREEMPT_RT.

## Build and Dependency Rules

- CMake is the only project build-system authority.
- Support host-native tests, Ubuntu-22.04-compatible aarch64 cross builds, and optional native target builds through presets/toolchain files.
- Cross-link against a versioned sysroot collected from the actual RK3588 Ubuntu 22.04 target/rootfs. Never substitute the cross-container filesystem for the target sysroot.
- Docker is a versioned toolchain runner. The host checkout is authoritative and build outputs remain host-owned and out-of-source.
- Pin every third-party dependency by an immutable tag plus commit, submodule commit, or vendored archive checksum. Normal builds must not fetch an unpinned branch.
- Do not add dependencies without documenting purpose, version, license, target availability, and test impact.
- Prefer standard C++20/C/POSIX and existing Linux facilities over new frameworks.

## Coding Rules

- All future physical tests use the operator-approved +/-2rpm (20 tenths rpm)
  band for measured near-zero feedback, including startup, standstill, stop,
  uncommanded wheels and cleanup. Do not restore an exact-zero feedback gate.
  Targets, safe command payloads, protocol values and active-motion requirements
  remain exact or independently bounded; decoder/unit-test exactness is unchanged.

- All project-owned C/C++ modules, tools and tests use the root `.clang-tidy`
  and `.clang-format`. Follow `docs/development/STATIC_ANALYSIS.md` for complete
  optional-build coverage, generated-code exceptions and warning triage.

- Use modern conservative C++ with explicit ownership and deterministic lifetime. C is acceptable at C library boundaries.
- No hidden mutable globals. Process-lifetime singletons require written justification and tests.
- Errors carry operation, device/node identity, and underlying error information; no silent failure.
- Every newly implemented project function has English Doxygen documentation. Public APIs document purpose, parameters, result, thread safety, and ownership/lifetime where relevant.
- Protocol-independent domain code must compile and test on x86_64 without Linux device access.
- High-rate loops must not emit unthrottled logs.
- Preserve raw protocol values in observations/diagnostics even when decoded into typed states.

## Testing and Verification

- Add or update tests before changing safety/arbitration semantics.
- Required layers: unit tests, `vcan` integration, target hardware tests, fault injection, and long-duration soak tests.
- Hardware motion tests begin unloaded/raised, with explicit zero-target preconditions and a physical emergency-stop path.
- Primary HIL SDO/NMT/control sequences run in the RK3588 application. Use JCAN mainly for passive capture and separately authorized small validation sends; use target candump as a second capture. Do not substitute a JCAN/Python sequencer for the application under test.
- Coordinate manual wheel phases through operator-visible application timing or an explicitly agreed manual sequence observed with candump. Do not rely on exact chat-message timing; confirm the actual wheel order before physical-axis sign-off.
- Every implementation phase must leave a reviewable, testable state and meet the acceptance criteria in the project plan.
- Before claiming completion, run the narrow changed tests, then applicable host build/tests, cross build, static analysis, and target smoke/HIL checks. Report any unavailable validation explicitly.
- Never encode a manufacturer-document contradiction as a safety fact; add a hardware validation record.

## Deployment and Security

- Production runs as a dedicated non-root `robot-control` user.
- Prefer group/udev ACLs for UART/GPIO and ordinary SocketCAN data access. Keep CAN interface creation, bitrate, restart-ms, and link-up in privileged boot/network configuration, not the application.
- Do not embed SSH passwords, keys, target addresses, or secrets in source or scripts.
- Development deployment is non-destructive by default and targets a versioned staging/release directory under `/opt/robot-control/`.
- Production is managed by systemd, logs to stdout/stderr for journald, handles SIGTERM, and uses bounded restart behavior.

## Agent Workflow

1. Read this file and the relevant design docs.
2. Inspect existing code and legacy evidence before editing; do not infer missing device mappings.
3. Keep each change within one phase/milestone and avoid speculative later-phase abstractions.
4. Add tests and documentation with the implementation.
5. Verify locally and report exact commands/results.
6. Do not flash, move a motor, alter target device-tree/network configuration, deploy to production, or change persistent drive parameters without explicit authorization.

## Latest powered-off CAN investigation

The late read-only request appears after application exit (TX314 to315). Isolated host vxcan demonstrates delivery400ms after socket close; virtual link-down prevents delivery in that setup. Target vxcan is unavailable, and matching rockchip_canfd source is absent from the checked headers directory. Exact queue/stop behavior remains unresolved; no production or physical-interface change was made. Keep the drive powered off and preserve failed/consumed trials. See docs/verification/P6_DELAYED_TX_INVESTIGATION.md.

The follow-up target-binary review confirms rockchip_canfd_tx_err_delay_work resubmits and self-schedules independently of socket lifetime. Supported ctrlmode mask0x17 excludes one-shot. Target ndo_stop cancels delayed work after controller stop/runtime-PM release; concurrency review is required. Disk-image notes match running kernel; exact full-tree commit remains unknown. No kernel/physical changes were made. See docs/verification/P6_ROCKCHIP_TX_WORKER_REVIEW.md.


## Latest F4 A5 disposition and ±2rpm revision (2026-09-17)

A4 exited before motion because transmitter-OFF failsafe candidates were mistaken
for right-throttle commands. Startup handling now waits within the original60s
budget and reports waiting_link/neutral_required/release_ch6/ready/arming; valid
neutral input and a fresh CH6 edge remain required. Once enabled, loss terminates
the trial without automatic rearm. Explicit8s motion hard limit/7950ms cutoff
was approved; the default motion window remains3s.

A5 (38800cfe, CLI15) observed actual flags0→4 during7089.866087ms right motion,
receive→zero22.167us, no restart/X1,2313 matching capture frames and full restoration.
It originally FAILED because one−1.6rpm stop sample exceeded±1.5rpm. That failure,
feedback_bad=1 and original raw evidence remain unchanged. User explicitly raised
the startup/stop band to±2rpm. Independent retrospective A5 analysis passes under
that criterion, including275.795564ms stable-band verification. F4 is accepted
under the revised criterion; F5/F6 remain pending, P10.3 OPEN. Operator confirmed
OFF, right direction/stop normal, left stationary, no abnormal sound. A1..A5
runners remain consumed; no new physical run occurred for this revision.

Control HIL now defaults to20 tenths rpm (accepted0..20); library defaults stay0.
Debug45/45, sanitizer45/45, scoped static, locked cross and ELF checks pass.
New b8f9192f artifact has not been deployed or run on target. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/standstill_20_revision/README.md.


## Latest F4 A7 — PASS with deferred stop-feedback review (2026-09-17)

New cd8bd1c5 artifact records selected-wheel negative stop-band excursions for
post-trial review after successful zero RPDO, bounded by±7.5rpm hard limit and
opposite-wheel±2rpm. Active motion/startup/cleanup guards and1s/150ms stable stop
checks remain enforced. Pending does not mean accepted. Debug/sanitizer45/45,
scoped static/cross/ELF/source-snapshot and target device-free smoke passed.
A6 hit7950ms cutoff; SBUS loss187.906ms later. User confirmed delayed shutdown.
A6−3.5/−3.8rpm review items remain pending; original incomplete trial preserved.
Fresh user-authorized A7 observed frame_lost during3.700s motion, receive→zero
49.584us, stable band verified280.680ms, no restart/X1/feedback review items.
1807 dual frames match,1006 RPDOs,36 writes/full restoration,CAN counters unchanged.
Operator OFF/right direction+stop normal/left stationary/no sound/transmitter OFF/
receiver continuously powered confirmed; optional action description is empty,
physical button timing unmeasured. Expected protective exit1 accepted independently.
Four missed periods/max lateness39.757ms retained; no hard-real-time claim.
A7 PASS,F4 accepted; F5/F6 remain pending,P10.3 OPEN. All A1..A7 runners consumed.
See docs/verification/evidence/p10_3_fault_recovery_20260917/motion_sbus_a7/README.md.


## Latest F5 UART A1 — INCOMPLETE (2026-09-17)

User authorized F5 by disconnecting only the SBUS signal port; fresh local START
confirmed USB/power/ground retained and transmitter ON. cd8bd1c5/CLI20 zero-only
runner used once. Signal interruption left42 raw bytes/one complete frame in the
last batch; after59.970ms the Reader partial_timeout3 produced discontinuity and
session2→3. Source revoked immediately (fault9); the recovery observer, which
accepts RF flags/valid-frame timeout only, exited before100ms timeout/recovery.
2276 matching frames,1339 all-zero RPDOs,36 writes/full baseline restoration,
no feedback violation or CAN counter changes. Operator OFF/stationary/no sound
confirmed. Short action response “是的” does not separately establish signal
reconnection. Both runners consumed, no retry. F5 remains OPEN; a future F5
observer must handle expected partial-frame interruption while retaining immediate
revocation and rejecting unrelated service-gap/backlog/transport errors. Preserve
this original incomplete run. F4 accepted; F5/F6 keep P10.3 OPEN. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/zero_uart_a1/README.md.


## Latest F5 A2 and extended-window preparation (2026-09-17)

13ccbd9d dedicated UART observer A2 handled the expected partial timeout and
completed1009.991ms hold, then exited in phase2 on source_fault2: two rejected
reconnect candidates1789.982ms after release. Not total-window exhaustion.
1694 matching frames,935 zero RPDOs,36 writes/full baseline restoration,one
expected discontinuity,no feedback failure/CAN counter changes. Operator OFF,
stationary wheels/no sound confirmed; action free text empty. A2 remains INCOMPLETE
and consumed; no automatic rerun. See zero_uart_a2/analysis.json.

User requests longer intervals/window. New bcd4cfe9 preparation supports F5-only
120s total,5s minimum hold,45s reconnect/challenge. It records initial rejected
candidates while authority stays revoked; INPUT_RESTORED marks healthy Disabled
input before nonneutralCH6. Hard failures and fresh rearm remain enforced.
Debug/sanitizer46/46,scoped static,cross/ELF/109-input snapshot checks pass. New
artifact not deployed or run on target; A3 scripts prepared authorized=false.
F5/F6 remain OPEN; F4 accepted. Fresh A3 authorization/readiness is required.
See docs/verification/evidence/p10_3_fault_recovery_20260917/uart_window_preparation/README.md.


## Latest F5 A3 — raw silence verified, recovery INCOMPLETE (2026-09-17)

User explicitly authorized A3; bcd4cfe9/CLI20 with120s total,5s hold,45s reconnect
used once after target identity/hash/smoke and fresh local START. Reader raw-byte
gap16.350s and5.010s hold verified; expected partial timeout revoked immediately.
After healthy Disabled input/INPUT_RESTORED, two rejected candidates470.023ms
later caused phase2/source_fault2 protected exit. Recorded axes remained neutral
and authority revoked. Not window expiration; physical framing-defect cause unknown.
3756 matching capture frames,2367 all-zero RPDOs,36 writes/full restoration,
no feedback failure/CAN counter change. Operator OFF/stationary/no sound confirmed;
action free text empty. Both markers consumed; no retry. Raw silence portion
verified,full nonneutral/fresh-rearm F5 recovery remains OPEN; F6 pending. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/zero_uart_a3/README.md.


## Latest F5 A4 — PASS with continuous stable recovery (2026-09-17)

User-authorized91d3ce99/CLI20,120s total/5s hold/45s reconnect. Phase2 repeated
parser rejections retain revoked authority and reset stable/challenge progress;
>=1s received healthy neutral Disabled input precedes each live recovery cue.
Debug/sanitizer46/46,scoped static,cross/ELF/109-input snapshot,target smoke pass.
A4 raw-byte silence10.430s and5.010s hold verified. One rejected reconnect candidate
retained/resynchronized; stable1s then nonneutral rejection,neutral,newCH6 and
quick-stop/Disable Voltage/fresh Disabled recovery all independently verified.
4722 matching frames,3039 zero RPDOs,36 writes/full restoration,no feedback failure
or CAN counter changes. Operator OFF/stationary/no sound/signal-only/USB-power-ground
unchanged/transmitter ON confirmed; optional action text empty. Both markers consumed.
F5 accepted within Reader raw-byte silence/zero-output recovery,not measured voltage
silence or100ms-trigger evidence (initial withdrawal was partial timeout). A1/A2/A3
remain incomplete. F6 remains pending; P10.3 OPEN. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/zero_uart_a4/README.md.


## Latest F6 A1 — PASS; bounded P10.3 HIL accepted (2026-09-17)

User-requested F6 uses91d3ce99/CLI20,right<=5rpm/<3s. Reused109-input verified
artifact; focused SIGTERM/right-throttle tests pass Debug and sanitizer,runner
identity/causality checks and target device-free smoke pass. Fresh local START,
one439.733ms right motion,automatic SIGTERM after312.680ms observed positive
feedback. Signal-operation→zero-send upper bound312.960us,internal shutdown9.736ms,
stable±2rpm stop253.266ms,process reap770.230ms,no restart/feedback review.
1507 matching dual-capture frames,798 RPDOs,36 writes/full baseline restoration,
CAN counters unchanged. Protective exit1/cause4/signal_exit2 independently accepted.
Operator OFF/right direction+stop normal/left stationary/no sound/transmitter and
receiver ON/no external fault confirmed. Both markers consumed; no retry permit.
F1–F6 and the planned bounded unloaded P10.3 HIL matrix are accepted; earlier OPEN
statements are historical. P6 qualification/soak,negative/dual-wheel/loaded motion,
production and kernel delayed-TX repair remain separate/unaccepted. F4 A6 historical
feedback-review items and all prior failures remain unchanged. See
 docs/verification/evidence/p10_3_fault_recovery_20260917/motion_sigterm_a1/README.md
and docs/verification/P10_3_HIL_CHECKPOINT.md for scope/evidence.


## P10.3 source/evidence delivery (2026-09-17)

Use docs/verification/P10_3_DELIVERY_BASELINE.md for the reviewed source scope,
compiled-input identity, evidence inventory and exact remote CI disposition.
Current acceptance is bounded unloaded P10.3 HIL; P6/long soak remain separate.
Older OPEN/unavailable-cross/powered notes retain their dated meaning. The latest
recorded operator disposition is OFF after F6 A1, not future powered readiness.


## Latest three-hour CANopen/JCAN soak acceptance (2026-09-18)

175 cycles/10807.299s and348246 order/payload-identical dual-capture frames pass
under the user-confirmed +/-2rpm standstill criterion. Nine right-feedback
samples at-0.3..+0.3rpm are accepted; raw evidence and the original strict-zero
audit remain historical. Operator confirms both wheels stationary/no abnormal
sound/drive OFF and approves this trial. No JCAN malformed-packet error,
disconnect/reconnect or sampled CAN counter increase occurred. No new run or
interface change accompanies this retrospective acceptance. See
docs/verification/evidence/rk3588_can_soak_20260918/README.md.
This is disabled CANopen lifecycle acceptance, not integrated SBUS/ControlLoop,
moving/loaded, kernel-repair or production acceptance. All runners remain consumed.
