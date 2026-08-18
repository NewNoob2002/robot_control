# Robot Control Middleware — Project Agent Guide

## Mission

Build a production-oriented, Linux-native low-level motion-control middleware for an RK3588 robot. The middleware owns CANopen/CiA402 drives, SBUS input, command arbitration, robot-level safety, health, diagnostics, and safe process lifecycle. ROS2 is an adapter and must not be the safety authority or the owner of the drive lifecycle.

## Current State

- Phases 0 through 3 are complete. Phase 2 provides pure command, arbitration,
  safety, and CiA402 domain libraries; Phase 3 provides policy-free Linux fd,
  poll, monotonic timer, signal, UART, and logging adapters. Host build, test,
  and static-check CI is implemented. SocketCAN, CANopen integration,
  cross/HIL/release CI, packaging, and deploy service are not implemented.
- `components/CANopenNode/` remains an unversioned baseline snapshot.
  EasyLogger's checksum-pinned core subset is integrated behind
  `service/logging`; replacing the mixed snapshot with a clean immutable
  upstream checkout remains release work. Provenance is recorded in
  `third_party/README.md`.
- `docs/ZLAC8015D_CANOPEN_NOTES.md` and the vendor PDFs are drive references; statements marked for hardware verification are not safety assumptions.
- The legacy STM32 project at `~/Desktop/workspace/STM32_PROJ/STM32G474_CANOPEN_Copy` is read-only architectural evidence, not a source tree to copy.
- Phase 1 build evidence and remaining target validation are recorded in
  `docs/build/PHASE1_BUILD_BASELINE.md`.
- Phase 2 behavior traceability and verification are recorded in
  `docs/verification/PHASE2_DOMAIN_BASELINE.md`.
- Phase 3 Linux adapter verification is recorded in
  `docs/verification/PHASE3_LINUX_PLATFORM_BASELINE.md`.

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
