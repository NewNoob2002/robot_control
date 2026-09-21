# Current checkpoint — 2026-09-21

Current software and verification: docs/verification/TEST_APP_BASELINE.md.
Current usage/configuration: docs/development/TEST_APP.md; handoff scope:
docs/plans/TEST_APP_HANDOFF.md. The preserved pre-app ground acceptance is in
docs/verification/GROUND_HANDOFF_CHECKPOINT_20260920.md. Historical entries below
retain their original scope and failures.

The Debug-only/default-OFF SBUS test app, startup configuration, rotating process
logger and separate device-configuration tool are implemented. IPC is deferred;
ROS2 remains an adapter design only. The app reads persistent parameters; only
explicit device-config --apply may write the reviewed parameters and request one
EEPROM save. Software validation is not physical-operation authorization.

User-approved app idle/uncommanded feedback fault threshold is +/-5rpm on
2026-09-21. Preflight, reauthorization, bounded stop and cleanup standstill
confirmation remain +/-2rpm; old HIL thresholds and evidence are unchanged.
Healthy powered standby holds zero without granting motion authority. No current
app hardware acceptance, production deployment or new persistent drive change is
authorized. Older app field logs are diagnostic evidence only.

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
- User-approved acceptance rule (2026-09-18): brief selected-wheel feedback jumps
  during a commanded startup or bounded stop are NORMAL_START_STOP_FEEDBACK_TRANSIENT,
  not by themselves a motion-direction or stop failure. Record every raw sample,
  phase, count, peak and timing; verify correct targets/direction, <=7.5rpm hard
  feedback bound, uncommanded wheels within +/-2rpm, no restart, and the existing
  <=1s stop deadline with >=150ms continuous +/-2rpm plus a fresh frame, full
  restoration and normal operator observations. Pre-enable standstill, sustained
  wrong-direction/overspeed, target errors and other safety failures are not exempt.
  Right-negative A1's single +2.9rpm post-zero sample is explicitly accepted;
  both negative single-wheel items are PASS. Historical exit1/feedback_bad/hard_bad
  and failed audits remain unchanged. This is acceptance guidance: deployed5f44146c
  still enforces the original guard; no online filter or new hardware permit is
  implied. See docs/plans/G3_RAISED_MOTION.md#启停短暂反馈跳变验收规则.

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

## Latest continuous SBUS/control one-hour archive (2026-09-18)

Hour A2 completed3600.367s with514256 SBUS frames,360002 exact-zero RPDOs,
13 restored objects and stable1668KiB post-ready RSS. Functional checks pass.
Strict global dual-capture order FAIL is preserved: one201/201/281 versus
281/201/201 window among518836 frames; per-ID/per-direction sequences match.
Cause unproven; user accepted this trial on2026-09-18 (acceptance.json). G2 accepted; no general capture-rule relaxation. Operator OFF,
stationary/no sound confirmed; runners consumed. See
docs/verification/evidence/control_zero_soak_20260918/hour_a2/README.md.
No moving/loaded endurance, P6 closure, production or kernel-repair claim.


## G3.1 software preparation (2026-09-18)

Eight signed HIL modes are implemented with3s hard motion/2950ms cutoff,
per-axis5rpm targets and±2rpm near-zero feedback. Command-profile rejection
now occurs before submission through an optional pure owner-thread predicate;
wire gate remains independent, runtime transport-failure policy unchanged.
Debug49/49 and final G3 replay pass; sanitizer initial47/49 plus failed-test
recheck2/2 pass (original timeout failures retained). Scoped static, locked cross,
ELF and123 compiled-input identity checks pass. Artifact5f44146c is not deployed.
G3.2/G3.3 remain physically untested; manual profile transitions need review,
new readiness/authorization and zero-target preflight. No ground/loaded permit.
See docs/plans/G3_RAISED_MOTION.md and
 docs/verification/evidence/g3_software_preparation_20260918/README.md.


## G3 new-artifact zero prerequisite (2026-09-18)

5f44146c is now staged on RK3588;123 inputs/hash/identity/device-free smoke pass.
A1 first0x6041 upload timed out with zero control cycles/no writes: operator
forgot drive power, original FAILED/INCOMPLETE preserved. Fresh local START
authorized A2 after actual power confirmation. A2 PASS:20s,3230 identical
dual-capture frames,2002 zero RPDOs,36 writes/full restoration, feedback
-0.2..+0.2rpm within+/-2rpm, fresh CH6, no CAN counter increase. Three missed
periods/max lateness35.901ms retained. Operator OFF/stationary/no sound confirmed.
All runners consumed. New-artifact zero prerequisite complete; G3 signed motion
still untested and needs reviewed manual coordination/new readiness/authorization.
See docs/verification/evidence/g3_raised_motion_20260918/zero_a2/README.md.


## G3 negative single-wheel disposition (2026-09-18)

Unchanged5f44146c, separate fresh local STARTs. Left-negative A1 PASS:2.960s,
1629 matching dual frames,58 directional samples,266.153ms stop verification,
36 writes/full restoration. Raw left-5.7..+1.5rpm/right-0.2..+0.4rpm.
Right-negative A1 FAILED:2.950s automatic cutoff, then one+2.9rpm sample
78.848ms after zero exceeded the opposite-direction+2rpm band; original
feedback_bad/hard_bad and exit1 preserved. No positive target or restart.
1466 matching dual frames,378.481ms stable stop,36 writes/full restoration
verified separately. Raw right-6.1..+2.9rpm/left0. No CAN counter increases.
Operators confirmed each selected wheel reverse/stop normal, opposite stationary,
no sound and drive OFF. Latest OFF is after right-negative A1, not new readiness.
3/4 missed periods and max lateness35.104/43.107ms retained; no hard-real-time claim.
All runners consumed. Left accepted; right-negative remains unaccepted; no
criterion change, automatic retry, dual-wheel or loaded permit. See
docs/verification/evidence/g3_raised_motion_20260918/left_negative_a1/README.md
and docs/verification/evidence/g3_raised_motion_20260918/right_negative_a1/README.md.


## G3 transient-feedback acceptance revision (2026-09-18)

User explicitly classifies these brief startup/stop feedback jumps as normal and
accepts right-negative A1. New acceptance PASS_USER_APPROVED_START_STOP_TRANSIENT:
one+2.9rpm sample78.848ms after zero;378.481ms stable stop,1466 matching frames,
36 writes/full restoration/no positive target or restart, normal operator/OFF.
Left-negative and right-negative are now accepted; the preceding right-FAILED
checkpoint is the preserved original verdict, not current acceptance. No raw
record, consumed runner, source guard or deployed binary changed; no new run.
See docs/verification/evidence/g3_raised_motion_20260918/transient_feedback_revision/README.md.
G3 dual-wheel/turn/differential and corresponding protection cases remain pending.


## Latest G3 dual-positive A1 — FAILED physical direction (2026-09-18)

5f44146c/local START, equal positive targets,2.950s,1200 matching dual frames,
57 simultaneous direction-feedback samples,301.700ms stable stop,36 writes/full
restoration/no CAN counter change. Software exit0/feedback_bad0, but operator
explicitly confirms left vehicle-forward/right vehicle-backward viewed from behind.
FAILED_PHYSICAL_DIRECTION; this is not the accepted brief feedback-transient rule.
Latest user disposition: drive OFF, both stopped, no sound. All runners consumed.
Current HIL binds left_sign=+1/right_sign=+1; RuntimePolicy supports symmetric
command/feedback polarity. Right vehicle-frame binding must be corrected with
consistent wire gate/trace/audit normalization before further vehicle-direction
qualification; no code, drive persistent direction setting or retry was applied.
Historical single-wheel signed results/user approvals stay unchanged but do not
prove vehicle-frame right-forward correctness. See
docs/verification/evidence/g3_raised_motion_20260918/dual_positive_a1/README.md.


## Latest G3 polarity correction and dual-forward acceptance (2026-09-18)

9b336936 injects right_sign=-1 (left+1) through existing RuntimeConfig, wire gate,
Trace and auditor; raw CAN records remain intact. Physical motion on can0 requires
explicit --right-sign; for this hardware use -1. Logical(5,5) means wire(5,-5).
Debug/sanitizer50/50, both20-case G3 matrices, scoped static, locked cross/ELF,
123-input identity and target device-free smoke verified. No persistent drive
parameter/interface change or online transient-filter change.
New zero A1 failed unpowered; A2 incomplete after duplicate CH6 (no nonzero
command). New console ZERO_ENABLED cue clarifies zero motion is expected; press
CH6 once, then wait. A3 PASS20s/3230 matching frames/2002 zero RPDOs/full restore.
Corrected dual-positive A1 accepted under the existing user-approved transient
rule:2.950s,1433 matching frames,57 simultaneous feedback samples,357.371ms stable
stop,36 writes/full restoration/no restart/CAN counter increase. One right raw
+2.5rpm/logical-2.5rpm57.701ms after zero is normal stop estimation transient;
original exit1/feedback_bad/hard_bad/primary_ok0 are preserved separately.
Operator confirms BOTH vehicle-forward/normal stop/no sound/drive OFF. This closes
the observed right-forward polarity defect; old5f44146c physical-direction failure
stays FAILED. Remaining vehicle-coordinate modes/protection/ground/loaded cases
remain open. All runners consumed; latest OFF is not future readiness. See
 docs/verification/evidence/g3_polarity_fix_20260918/README.md.

## Latest G3 dual-wheel UART A2 — accepted (2026-09-20)

Fresh chat readiness and local START, unchanged9b336936/right_sign=-1.
Signal-only interruption during759.994ms dual-forward motion causes100ms valid-frame
timeout before cutoff; expiry-threshold-to-zero15.024us, stable stop410.414ms.
1513 matching dual-capture frames,36 writes/full restoration,CAN counters unchanged.
One right raw+2.6rpm/logical-2.6rpm sample110.611ms after zero is accepted under
the existing start/stop transient criterion; original feedback_bad/hard_bad/exit1
and failed auditor remain unchanged. Operator OFF/both forward+stop normal/no sound/
signal-only/power-ground-USB unchanged/signal restored confirms disposition.
Both runners consumed; no automatic retry or new powered-readiness inference.
No RF-loss/rearm/electrical-voltage claim. Differential manual-input transitions
and other untested G3 cases remain separate; G3 overall is not closed. See
docs/verification/evidence/g3_remaining_motion_20260918/dual_uart_a2/README.md.

## Latest G3 dual-wheel RF A1/A2 — INCOMPLETE (2026-09-20)

A1 waited60s with0 UART bytes/frames, no enable/motion,6003 zero RPDOs,
8989 matching capture frames and full restoration. Operator identifies SBUS
data line not reconnected after UART A2; this corrects the previous terminal
reconnection claim only, preserving raw evidence. Operator explicitly authorized
new A2. A2 supervisor forwards input-status reasons; runtime9b336936 unchanged.
Fresh local START;2950.037ms dual-forward motion ends at automatic cutoff,
frame_lost first arrives279.903ms after zero. RF loss during motion is NOT
accepted.1602 matching frames,326.655ms stable stop,36 writes/full restoration,
no feedback failure/restart/CAN counter increase. Operator OFF/both forward and
stop normal/no sound/transmitter OFF/receiver powered/wiring unchanged confirmed.
A1/A2 consumed; no automatic retry or extended motion window. Future RF trial
needs reviewed manual timing and fresh readiness. G3 overall remains open. See
docs/verification/evidence/g3_remaining_motion_20260918/dual_rf_a2/README.md.

## Latest G3 dual-wheel RF A3 — accepted (2026-09-20)

User explicitly requested retry without program/window changes. Unchanged9b336936,
right_sign=-1,<=5rpm/<3s; new local START and one-shot A3. Actual frame_lost during
2489.997ms motion,receive-to-zero19.827us,stable stop367.524ms,no restart/X1/UART
discontinuity.1446 matching dual-capture frames,36 writes/full restoration,CAN
counters unchanged. One right raw+3.4rpm/logical-3.4rpm sample67.908ms after zero
accepted under existing start/stop transient rule; original feedback_bad/hard_bad,
exit1 and failed auditor preserved. Operator OFF/both forward+stop normal/no sound/
transmitter OFF/receiver continuously powered/wiring unchanged confirmed. Both
runners consumed; A1/A2 remain incomplete. Dual-forward RF-loss stop accepted;
no recovery/rearm,ground/loaded or overall G3 closure. See
docs/verification/evidence/g3_remaining_motion_20260918/dual_rf_a3/README.md.

## G3 differential preflight — blocked manual transition (2026-09-20)

Six gradual manual paths (left/right; steering-first,throttle-first,simultaneous)
reproduce rejection before the valid unequal-wheel endpoint. First rejected
targets are(0,3),(3,0)or(3,3); current strict differential gate rejects these.
123 artifact-source inputs match; existing Source/gate/replay host checks3/3 pass.
No runtime/window change or hardware operation. Proposed differential-only
straight-to-turn qualification approach needs coordinated checks/auditor and
fresh verified artifact before physical testing; it is not implemented. See
docs/verification/evidence/g3_remaining_motion_20260918/DIFFERENTIAL_PREFLIGHT.md.

## G3 differential approach software — verified (2026-09-20)

User approved positive equal-wheel approach for differential modes. New51f94c1f
shares left<=right/right<=left checks across owner and wire gate; normal success
also requires>=2 fresh unequal-command/directionally ordered dual feedback samples.
Straight-only/equal-feedback/wrong-order evidence cannot pass. Source unchanged;
3s/2950ms timer starts at FIRST nonzero including straight; zero closure/no restart,
5rpm bounds and stop/restoration rules unchanged. Debug/sanitizer53/53 each,
two14-case differential matrices per configuration, scoped static/cross/ELF and
123 compiled-input identity checks pass. Five target device-free checks pass in
new staging g3-differential-51f94c1f-20260920; old artifacts/trials unchanged.
No new physical run. New artifact zero prerequisite and left/right differential
HIL need fresh trial/readiness; G3 remains open. See
docs/verification/evidence/g3_differential_preparation_20260920/README.md.


## 2026-09-20 用户批准的差速功能验收

手柄约20%仅为操作建议，不作为验收门槛。已在原3秒/5rpm边界内发送正确正向不等速目标，并有至少两个新的同向且大小关系正确的双轮反馈，随后安全停止、无重启、完整恢复及正常现场反馈，可接受差速功能；后续越界候选被拒绝不抹去此前有效证据。在线保护及原始失败/退出码不变，无新运行授权。
左差速A1按此规则复核PASS：6个有效差速样本，运动609.998ms，稳定停止355.479ms，1209帧双采集一致，36次易失写入完整恢复，CAN计数不变，现场OFF确认。A2没有非零RPDO，仍未完成，不能用于运动验收。右差速仍待验证。


## 2026-09-20 右差速 A2 功能验收 PASS

用户确认先推满前进油门再右转。51f94c1f/CLI20/right-sign -1，运动1460.002ms，7个有效右慢左快反馈样本；后续CH1=1357/CH3=1800产生(5,0)候选并被发送前拒绝。按用户批准的差速功能准则接受已完成的差速证据，原始exit1/primary_ok0不变。稳定停止300.536ms，无反馈违规/重启；1121帧双采集一致，36次易失写入完整恢复，CAN计数不变；现场OFF确认双轮前进/停止正常、无异响、驱动断电。左右差速功能项现已接受；无新软件修改、无新硬件运行授权，不扩展到负载/落地/生产。证据：docs/verification/evidence/g3_differential_hil_20260920/differential_right_a2/revised-acceptance.json。


## 2026-09-20 单侧故障实机项暂停及地面准备状态

用户明确表示暂无相关设备，要求暂停存档，待设备具备后再测试。单侧驱动故障导致双轴抑制的物理项标记 DEFERRED_EQUIPMENT_UNAVAILABLE；既有vcan软件证据保留，实机未验证，不标记PASS，也不消除G3剩余缺口。恢复时需确认厂家支持的单侧刺激、停止/恢复路径、具体产物、独立捕获和新的现场授权；所有旧runner保持消耗状态。

常规架空方向/差速及双轮X1、RF失联、UART断线、SIGTERM已有对应验收，不为本次暂停重复运行。接下来可推进G4：轮径/传动比/rpm含义与实际地速、机械与供电检查、独立急停/失电制动、CAN延迟TX残余及最终运行入口的停止方案、场地净空和距离/速度/时间边界。单侧故障延期若要带入首次地面试验，必须在具体准入单中明确审查并接受残余，不能把本次暂停视为地面准入。

G4条件闭合后另行授权G5：无载低速短时前进/正常停止并测位移与停车距离，随后后退、左右转向/差速；地面急停与失联停车效果按独立有界试验单逐项验证。连续行驶、带载和长期运行属于后续阶段；阶段长稳1小时、主要功能完成后最终5小时整机稳定性计划不变。电气bus-off等尚缺设备的项目独立保留未验证状态。当前无新硬件运行，无地面运动授权。


## 2026-09-20 电机图纸与停止方案延期

用户提供ZLLG14ASM800 V2.0图纸（外径358±2mm，48V，额定150rpm），并要求停止方案先存档、拟地面观察复现。已记录延期未验证；独立停止链及最终组合验证没有因此通过。轮端速度换算/有效滚动周长与现场停止及隔离条件仍待明确，未启动地面测试。资料及边界见docs/verification/evidence/g4_ground_preparation_20260920/README.md。

2026-09-20现场补充：断驱动动力后可自由转动、车辆可能滑行；用户说明物理急停在保留动力时可锁轴。两种停止方式分开记录，锁轴的具体回路与地面保持效果尚待核对，不将其视为失电制动。地面断电收尾前需机械防溜。当前仍未启动地面试验。

## 2026-09-20 地面静止零目标 X1 A1 — PASS

51f94c1f/CLI20/right-sign -1，3759帧双捕获完全一致，2368个RPDO目标全部精确为零。X1保持1009.999795ms，非中立CH6被拒绝，中立新授权后零目标重新使能并保持1009.996817ms。36次易失写入、写后回读及原始基线恢复通过，CAN计数不变。现场OFF确认车辆全程静止、防溜持续有效、无异响、驱动已断电。原始exit0，单次执行器已消耗。

仅接受有机械防溜条件下的静止零目标X1撤权及恢复不自启；未施加外力，不能证明抗外力锁轴/失电制动或地面停车距离。没有地面行驶证据，CAN延迟TX与单侧故障延期缺口保持。证据：docs/verification/evidence/g4_ground_preparation_20260920/zero_x1_a1/acceptance.json。

## 2026-09-20 首次地面前进 A1 结果

51f94c1f/CLI20/right-sign -1，原始exit0，双轮正向运动2950.030ms，零目标后410.140ms通过连续近零带及新帧确认；这是反馈停止验证时延，不是直接测得的地面机械停车时间。1362帧双采集一致，36次易失写入完整恢复，CAN计数不变；3次missed periods/最大迟到39.098ms保留。操作员OFF确认双轮前进/停止正常、无异响、驱动断电。

现场原话：“没有X1介入档，没有超越1米线，实际运动距离应该再25cm左右，停止到静止的距离在1cm”。按报告记录约0.25m行程、约0.01m停车位移、无X1介入及未越1m；没有提供尺量/录像等方法或物理停车时间，围挡接触问题未明确回答。协议及观察到的前进/停止功能通过，距离测量验收仍待澄清，不将估计作为精密测量或通用停车上界。无需自动重测，也不据此启动下一项。

原始证据与分项判定：docs/verification/evidence/g5_ground_forward_20260920/forward_a1/ground-review.json。单次runner消耗完毕。CAN延迟TX及单侧故障仍延期；未证明故障修复、带载/连续行驶或G5全部完成。

## 2026-09-20 地面前进 A1 现场补充与最终范围

用户补充“目测的，没有接触围挡”。行程约25cm、停车位移约1cm均为目测估计；确认无围挡接触。结合协议/双捕获/完整恢复及现场正常前进停止，接受本次有界无载地面前进与停止功能。精确停车距离、机械停车时间及可用于后续试验的保证停车上限仍未建立，不将目测值外推为测量结论。该补充完善原记录，不修改原始日志，不重跑、不授权下一项。原“测量方法/围挡接触未回答”段落由本补充澄清。

## 2026-09-20 地面后退 A1 — 有界功能通过

51f94c1f/CLI20/right-sign -1，原始exit0。双轮逻辑负目标运动2950.006ms，各57个有效反向运动反馈；零目标后400.223ms完成稳定近零及新帧确认，无重启或反馈违规。1385帧双捕获一致、36次易失写入完整恢复，CAN计数不变；4次missed periods、最大迟到40.148ms保留，不声明硬实时。

操作员报告约25cm后退、1–2cm停车位移，无X1介入、未接触围挡、未越1m线；OFF确认双轮后退/停止正常、无异响、驱动断电。本轮未单独说明测量方法，距离仅作为近似现场报告，不提升为仪器测量或保证停车上限；反馈验证时延不等同实测机械停车时间。

接受本次限定无载地面后退与正常停止功能。一次性执行器消耗完毕，不自动重跑或启动下一项。精确速度尺度、停车距离/时间、故障残余及地面转向/差速仍独立保留；不关闭G5整体。证据：docs/verification/evidence/g5_ground_reverse_20260920/reverse_a1/ground-review.json。

## 2026-09-20 地面原地左转 A1 — 有界功能通过

51f94c1f/CLI20/right-sign -1，原始exit0。左轮后退、右轮前进，非零运动2950.011ms，各57个有效方向反馈；零目标后510.090ms完成稳定近零及新帧确认，无重启或反馈违规。2383帧双捕获一致，36次易失写入及完整恢复通过，CAN计数不变；3次missed periods、最大迟到33.105ms保留，无硬实时声明。

现场目测左转约40度，停止后几乎无可见继续运动，中心偏移1–2cm；无X1介入、无围挡接触、中心未越1m边界。OFF确认左轮后退/右轮前进及停止正常、无异响、驱动断电。角度与位移为目测，余转未量化，不声明精确停车转角/机械停转时间，也不从单次估算推导轮距或速度标定。

接受限定无载地面左转与正常停止功能；右转及地面差速仍待对应试验，G5未整体关闭。所有本轮执行器已消耗，无自动重试或新运行。证据：docs/verification/evidence/g5_ground_turn_20260920/left_a1/ground-review.json。

## 2026-09-20 地面原地右转 A1 — 有界功能通过

51f94c1f/CLI20/right-sign -1，原始exit0。左轮前进、右轮后退，非零运动2950.011ms，各57个有效方向反馈；零目标后470.097ms完成稳定近零及新帧确认，无重启或反馈违规。1695帧双捕获一致，36次易失写入及完整恢复通过，CAN计数不变；4次missed periods、最大迟到43.626ms保留，不声明硬实时。

现场目测右转约45度，停止后余转几乎没有，中心偏移约1cm；记录无X1介入、未接触围挡、中心未越1m边界。OFF确认左轮前进/右轮后退及停止正常、无异响、驱动断电。角度和位移为目测，余转未量化，不声明精确机械停车角度/时间，不以左右40/45度估计推断系统偏差。

接受限定无载地面右转与正常停止功能。地面前进、后退、原地左右转向分项已有通过证据，地面左右差速仍待独立测试，G5未整体关闭。所有本轮执行器已消耗，不自动重试或启动下一项。证据：docs/verification/evidence/g5_ground_turn_20260920/right_a1/ground-review.json。

## 2026-09-20 地面左差速 A1 手推干扰与恢复缺口

用户报告意外用手推车并授权一次重测。原试次在运动准备完成之前退出，enabled_samples=0、反馈门失败；清理静止反馈检查失败，restore_ok=0/control_hil_cleanup_unverified/operator_power_off_required。不能将此情况按普通已恢复试次直接重跑。原失败和所有数据保留；用户重测授权保留，先确认实际停稳/机械防溜/驱动断电，再核对当前参数与原始基线并准备有界恢复。不把用户手推说明当作已证明唯一故障原因。本轮没有新增恢复写入或重测。

2026-09-20 地面左差速A1后的禁能基线恢复通过：18次限定写入及完整回读，无使能/RPDO；345目标帧匹配JCAN410帧中offset65的完整连续段，保留前缀；CAN计数不变，现场静止/防溜/无声/OFF确认。原A1失败不改写；按既有一次重测授权准备left_a2，等待新现场START。见docs/verification/evidence/g5_ground_differential_20260920/baseline_restore_a1/README.md。

## 2026-09-20 地面左差速 A2 — 按已批准规则验收通过

51f94c1f/CLI20/right-sign -1，先直行后左差速共1779.997ms，13个独立有效左慢右快反馈样本。后续CH1=679/CH3=1800产生(0,5)单轮候选，被提交前拒绝；不是超时或无差速证据。保留原始exit1/primary_ok0。

零目标后412.482ms出现唯一右raw+2.2rpm/逻辑−2.2rpm样本（同帧左−0.4rpm），全程未超过±7.5rpm；该帧位于有界停止且早于稳定段，712.426ms完成双轴连续近零及新帧确认。按用户已批准NORMAL_START_STOP_FEEDBACK_TRANSIENT规则接受，feedback_bad1/feedback_hard_bad1及所有raw保持不变。没有在线保护修改。独立审计分别使用应用与线侧时钟，并验证超速和重复异常篡改被拒绝。

1274帧双捕获一致，36次易失写入与原始基线完整恢复，CAN计数不变，无重启。现场目测前进约20cm、左转弯位移1–2cm、几乎无余动，无X1介入/接触/越1m边界；OFF确认双轮前进停止正常、无异响、驱动断电。保留现场原话，不将转弯位移解释为精确曲率或停车距离。

本次有界无载地面左差速功能通过。原A1手推失败及清理失败保留，独立禁能基线恢复另有通过记录。A2一次性执行器消耗完毕，无自动重试；右差速及剩余G5项目仍待相应证据。复核脚本audit-revised.py，结果revised-acceptance.json。

## 2026-09-20 地面右差速 A1 — 按既定功能规则通过

51f94c1f/CLI20/right-sign -1，运动949.935ms，7个独立有效右慢左快反馈样本。随后CH1=1330/CH3=1800产生(5,0)单轮候选，发送前拒绝并保护停止；原始exit1/primary_ok0保留。无反馈违规/复核项，424.132ms完成稳定近零及新帧确认，无重启。1238帧双捕获一致，36次易失写入完整恢复，CAN计数不变；3次missed periods/最大迟到37.767ms保留。

用户目测报告前进约20cm、右转停止弯位移1–2cm、余角几乎没有，无X1介入、无接触、中心未越1m线；OFF确认双轮前进停止正常、无异响、驱动断电。保留原话及目测属性，不将20cm用于校准轮端速度尺度，亦不据此证明精确停车距离。

接受本次限定无载地面右差速功能。地面前进/后退/原地左右转向/左右差速这六类基本有界运动均有分项验收；这不等于G5整体或整机安全验收完成。地面故障停车、精确尺度/停车测量、延期单侧故障/CAN延迟TX残余和后续连续/带载/长稳仍分开追踪。不自动启动下一试验，runner已消耗。证据：docs/verification/evidence/g5_ground_differential_20260920/right_a1/ground-review.json。
