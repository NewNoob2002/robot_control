# Current checkpoint — 2026-10-27

Current software and verification: docs/verification/TEST_APP_BASELINE.md.
Usage/configuration: docs/development/TEST_APP.md; handoff scope:
docs/plans/TEST_APP_HANDOFF.md. Pre-app ground acceptance:
docs/verification/GROUND_HANDOFF_CHECKPOINT_20260920.md.

- Implemented: Debug-only/default-OFF SBUS test app, startup configuration, rotating
  process logger, separate device-configuration tool, optional read-only Unix SEQPACKET
  IPC and a separate Humble status/probe node (docs/development/ROS2_IPC.md), plus
  robot_control_ros2 packaging, ros2 run/launch and a host-local 500ms receipt monitor
  (docs/development/ROS2_DIAGNOSTICS.md, docs/verification/ROS2_DIAGNOSTICS_BASELINE.md).
- Verified (device-free): Debug and sanitizer 43/43 each on the 2026-10-12 rerun, pinned Humble
  DDS regression, board-to-host status over Fast DDS TCP and explicit UDP. Cross was not
  rerun; remote CI has not run the newest changes; source-state age across hosts and remote
  probes remain unverified. The new package has not been staged on the board.
- UDP evidence: one user-authorized IPv4 rule on wlan0, board 192.168.110.161 to host
  192.168.110.198 UDP 54710:54711 (docs/verification/evidence/ipc_udp_allow_20261010/README.md);
  pre-rule diagnosis in docs/verification/evidence/ipc_network_20261010/README.md. Default
  DDS and neighboring ports stay denied; no other firewall action is authorized.
- Stage review and next gates: docs/verification/evidence/ipc_stage_delivery_20261010/README.md,
  docs/plans/ROS2_IPC_NEXT_STEPS.md. Proposed challenge/authorization contracts
  (docs/plans/ROS2_EXTERNAL_AUTHORITY_CONTRACT.md) are documented, not implemented motion authority.
- External motion remains unconnected. Physical CAN/UART/drive acceptance is separate.
  Software validation is not physical-operation authorization.
- The app reads persistent parameters; only explicit device-config --apply may write the
  reviewed parameters and request one EEPROM save.
- Thresholds: app idle/uncommanded feedback fault +/-5rpm (user-approved 2026-09-21);
  preflight, reauthorization, bounded stop and cleanup standstill +/-2rpm. Healthy powered
  standby holds zero without granting motion authority.
- Not authorized: current app hardware acceptance, production deployment, new persistent
  drive change. Older app field logs are diagnostic evidence only.

Dated HIL/hardware history is in docs/verification/AGENTS_HISTORY.md.


# Robot Control Middleware — Project Agent Guide

## Mission

Build a production-oriented, Linux-native low-level motion-control middleware for an RK3588 robot. The middleware owns CANopen/CiA402 drives, SBUS input, command arbitration, robot-level safety, health, diagnostics, and safe process lifecycle. ROS2 is an adapter and must not be the safety authority or the owner of the drive lifecycle.

## Current State

- Phases 0–5 are complete; Phase 6 remains open (docs/verification/PHASE6_CHECKPOINT.md,
  docs/verification/evidence/README.md). Phase plans: docs/build/PHASE1_BUILD_BASELINE.md,
  docs/verification/PHASE2_DOMAIN_BASELINE.md, docs/verification/PHASE3_LINUX_PLATFORM_BASELINE.md,
  docs/plans/PHASE4_SOCKETCAN_FOUNDATION.md, docs/plans/PHASE5_CANOPEN_INTEGRATION.md,
  docs/plans/PHASE6_ZLAC8015D_QUALIFICATION.md.
- P5.6 read-only commissioning: docs/verification/P5_6_READ_ONLY_COMMISSIONING_BASELINE.md.
  P6.1 fixture: docs/verification/P6_1_CONTRACT_FIXTURE_READ_ONLY_BASELINE.md. P6.2 protocol:
  docs/verification/P6_2_ZLAC_PROTOCOL_SEMANTICS_BASELINE.md. P6.3 executor:
  docs/verification/P6_3_BOUNDED_QUALIFICATION_EXECUTOR_BASELINE.md.
- P8-R (separate Debug-only/default-OFF CANopen runtime library, guarded dual-axis RPDO,
  no SDO/NMT configuration, no automatic fault reset, no production entry point) is accepted
  within its software/vcan scope: docs/verification/P8_R_RUNTIME_BASELINE.md.
- P9.1 SBUS parser: docs/verification/P9_1_SBUS_PARSER_BASELINE.md. P9.2 UART/receive-only
  observer is CLOSED in its scope: docs/verification/P9_2_SBUS_UART_BASELINE.md,
  docs/verification/evidence/p9_2_sbus_manual_20260916/RESULT.md (electrical measurements and
  physical USB unplug not claimed). P9.3 source/health/mapping is CLOSED within receive-only
  input scope: docs/verification/P9_3_SBUS_SOURCE_BASELINE.md. Neither grants motion authority.
- P10.1 offline control cycle accepted: docs/verification/P10_1_CONTROL_CYCLE_BASELINE.md.
  P10.2 software/vcan closure: docs/verification/P10_2_CONTROL_LOOP_BASELINE.md. The planned
  bounded unloaded P10.3 HIL matrix (F1–F6) is accepted; see docs/verification/P10_3_HIL_CHECKPOINT.md
  and docs/verification/P10_3_DELIVERY_BASELINE.md. Ground forward/reverse/spot-turn/differential
  bounded unloaded trials have per-item acceptance in the history log; G5 as a whole, P6
  qualification/soak and production are not closed.
- Logging uses checksum-pinned spdlog v1.17.0 behind `service/logging`; CANopen dependency
  provenance and zero-local-patch status are in `third_party/README.md`.
- `docs/ZLAC8015D_CANOPEN_NOTES.md` and the vendor PDFs are drive references; statements marked
  for hardware verification are not safety assumptions.
- The legacy STM32 project at `~/Desktop/workspace/STM32_PROJ/STM32G474_CANOPEN_Copy` is
  read-only architectural evidence, not a source tree to copy.
- Full dated HIL/hardware results: docs/verification/AGENTS_HISTORY.md.

## Active approvals and restrictions

Distilled from the historical log; the log keeps the original wording and evidence.

- No production daemon, full SBUS/M4 pipeline, persistent drive change, loaded operation or
  ground motion is authorized. Archived authorizations are evidence only, never a permit; every
  runner/one-shot marker recorded as consumed stays consumed, with no automatic retry or longer
  window. The latest recorded operator OFF disposition is not future powered readiness.
- Qualification stays Debug-only/default-OFF. The independent can0 inhibitor is a userspace
  mitigation, not a kernel repair; it never brings an interface up or sends CAN. The
  rockchip_canfd delayed-TX worker/stop concurrency is unresolved (docs/verification/P6_DELAYED_TX_INVESTIGATION.md,
  docs/verification/P6_ROCKCHIP_TX_WORKER_REVIEW.md); keep the drive powered off for that
  investigation and make no kernel/physical-interface change.
- Near-zero feedback band is +/-2rpm (20 tenths) for physical tests, including startup,
  standstill, stop, uncommanded wheels and cleanup; Control HIL defaults to 20 tenths while
  library defaults stay 0. The earlier +/-1.5rpm and +/-1rpm criteria are superseded. App
  idle/uncommanded fault threshold is +/-5rpm (2026-09-21). Raw values are never altered.
- NORMAL_START_STOP_FEEDBACK_TRANSIENT (full rule under Coding Rules) is acceptance guidance
  only; it implies no online filter and no new hardware permit.
- Differential acceptance (user-approved 2026-09-20): a ~20% stick is advice, not a gate.
  Correct unequal positive targets within the 3s/5rpm bound plus >=2 fresh, same-direction,
  correctly ordered dual-wheel feedback samples, safe stop, no restart, full restoration and
  normal operator report suffice; later out-of-bound rejected candidates do not erase that
  evidence. Online protection and original exit codes are unchanged.
- Motion bounds: 3s hard window with 2950ms cutoff, per-axis 5rpm targets; the F4 explicit
  8s/7950ms window was approved only for that case. Physical motion on can0 requires explicit
  `--right-sign -1` for this hardware (logical (5,5) is wire (5,-5)).
- Strict global dual-capture ordering is not relaxed in general; per-ID/per-direction
  sequences are checked, and only trials the user explicitly accepted are accepted.
  Missed periods/lateness are retained; no hard-real-time claim.
- F5 acceptance covers Reader raw-byte silence/zero-output recovery only, not measured voltage
  silence or 100ms-trigger evidence. RF loss during motion and UART interruption are accepted
  only for the recorded bounded cases.
- DEFERRED_EQUIPMENT_UNAVAILABLE (user, 2026-09-20): the physical single-side drive-fault
  inhibition item is untested, not PASS; resuming needs a vendor-supported stimulus, stop/recovery
  path, artifact, independent capture and new on-site authorization. The motor-drawing
  (ZLLG14ASM800 V2.0) stop-scheme verification is also deferred and unverified.
- Ground work: G4 prerequisites (wheel diameter/ratio/rpm-to-speed, mechanical and power checks,
  independent e-stop / power-loss brake, delayed-TX residual, final entry-point stop plan,
  clearance and distance/speed/time bounds) must close, and G5 needs separate authorization.
  Deferred single-side fault must be explicitly reviewed in any ground admission sheet. Power-off
  free-wheeling and lock-on-e-stop are separate behaviors (lock is not a power-off brake);
  mechanical anti-roll is required before ground power-down. Visual distances (~25cm, ~1cm
  stop, ~40/45deg turns) are not measurements or guaranteed stop limits. Continuous, loaded and
  long-duration work, stage 1-hour soaks and the final 5-hour whole-machine stability plan
  remain future items; electrical bus-off and other unavailable-equipment items stay unverified.
- Long soak is deferred (operator, 2026-09-15); V4 authorization is retired and any later soak
  needs a new artifact/preflight and explicit authorization. Accepted three-hour CANopen soak
  and one-hour SBUS/control archive are disabled/zero-target lifecycle evidence, not
  moving/loaded, kernel-repair or production acceptance.
- ROS2: external motion is unconnected; callbacks never send CAN. Read-only diagnostics need no
  authorization generation. No new hardware or firewall action beyond the single UDP rule above.

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
- Immutable, versioned snapshots carrying sequence/generation/timestamp are required for inputs that reach arbitration, safety or drive decisions; read-only diagnostics and logging paths need not carry authorization generations.
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

Dated HIL/hardware results moved out of this file (original wording, chronological): docs/verification/AGENTS_HISTORY.md.
