# RK3588 Linux Motion-Control Middleware Implementation Plan

## Planning Status

- **Mode:** `$plan` direct mode; planning only, no product code implemented.
- **Target result:** a staged, testable architecture and execution plan that preserves proven STM32 control/safety semantics while replacing HAL/RTOS mechanisms with Linux-native interfaces.
- **Stop condition:** architecture boundaries, build/sysroot path, execution model, safety behavior, deployment, testing, risks, open hardware questions, and the first implementation step are explicit enough for downstream agents to execute without redesign.

## Requirements Summary

The RK3588 process is the robot's low-level control authority. It must own SocketCAN/CANopen/CiA402, SBUS, arbitration, robot safety, diagnostics, and safe lifecycle independently of ROS2. The host repository is authoritative; Docker cross-compiles reproducibly against the RK3588 Ubuntu 22.04 target userspace; artifacts deploy over SSH and later run under systemd as a non-root service. Hardware configuration remains in the kernel/device tree.

Key behavioral requirements:

1. A single owner produces final drive commands after arbitration and safety evaluation.
2. ROS2 and SBUS are timestamped command producers, never drive writers.
3. Communication loss or stale observations result in zero/inhibit and bounded drive-safe action.
4. Recovery never causes immediate motion; it requires zero/neutral confirmation and a fresh authorization generation.
5. CANopen/CiA402 continue coherently when ROS2 disappears or restarts.
6. Protocol logic is host-testable without devices; Linux adapters are integration-tested with `vcan`, PTYs, and hardware.
7. No target binary links unintentionally against Ubuntu 22.04 userspace.

## Acceptance Criteria

- A documented acyclic dependency graph is enforced by CMake targets and include boundaries.
- `host-test` configures/builds/runs all protocol-independent tests on x86_64.
- `rk3588-debug` and `rk3588-release` link only against a manifest-identified Ubuntu 22.04 aarch64 sysroot collected from the board image; ELF interpreter, dynamic dependencies, and symbol versions are checked automatically.
- `vcan` integration proves CAN open/read/write, error propagation, timestamps/counters, process restart, and loss injection without hardware.
- SBUS parser tests cover split/concatenated/corrupt/noisy streams, lost/failsafe flags, stale timeout, and recovery neutral gate.
- Arbitration tests cover every source-validity/priority/handover case including the 150 ms compatibility zero-hold boundary and generation replay rejection.
- Safety transition tests cover every state/input combination and verify zero/inhibit/quick-stop/disable policy.
- CiA402 decoder and planner tests cover all standard masked states, illegal states, fault reset gating, transition timeout, stale status, and mode mismatch.
- On hardware, startup never sends a non-zero target before CAN, NMT, heartbeat, PDO freshness, mode, CiA402 enable sequence, safety authorization, and zero target are valid.
- SIGTERM integration proves command intake inhibition and completion of the configured safe-stop sequence within its deadline; forced-kill tests prove drive-side communication-loss protection stops motion independently.
- ROS2 process/node restart does not restart CANopen core and produces no unintended motion.
- A 4-hour initial soak has zero unintended state transitions, zero silent missed deadlines, bounded RSS growth, and diagnostic evidence for CAN/control latency.

---

# 1. Current Repository Assessment

## Existing

- `components/CANopenNode/` is a source snapshot of the portable CANopenNode stack. Its own README states that target drivers are external (`components/CANopenNode/README.md:43-50,121-159`).
- `components/EasyLogger/` is a broad embedded-oriented snapshot including unrelated MCU demos; it is not integrated.
- `docs/ZLAC8015D_CANOPEN_NOTES.md` is a valuable, explicit discrepancy/verification record. It documents dual-axis node behavior, CANopen objects, heartbeat, CiA402 and PDO assumptions (`docs/ZLAC8015D_CANOPEN_NOTES.md:14-24,74-119,121-236`).
- Two ZLAC8015D vendor PDFs are present.
- Empty placeholder directories exist under `config/`, `platform/hal/`, and `user/*`.
- `scripts/build.sh:1-14` invokes `rk3588-cross`, hardcodes a wrong host mount, creates an in-source `build/`, references a missing toolchain file, omits UID/GID handling, and uses unversioned image naming.
- The inspected image `rk3588-cross:latest` is Ubuntu 22.04 amd64 with GCC 11.4 cross tools and CMake 3.22.1. It matches the target distribution family but remains a compiler environment, not a substitute for the board-derived sysroot.

## Missing / Broken

- No root `CMakeLists.txt`, presets, toolchain, source targets, tests, CI, packaging, service, deploy workflow, or usable build.
- Empty `.git/` metadata means this path is not currently a valid Git repository. Dependency provenance and project history cannot yet be trusted.
- Vendored CANopenNode and EasyLogger have no recorded upstream commit/tag provenance in the project.
- CANopenLinux is absent.
- Target rootfs/sysroot, ABI manifest, target package list, ROS2 distribution, device names, permissions, and systemd integration are unresolved.

## Consequence

Treat this repository as a planning/provenance recovery baseline, not as an established Linux implementation. Preserve the drive notes; do not preserve the placeholder `user/`/`platform/hal` structure merely for familiarity.

---

# 2. Legacy STM32 Architecture Findings

## Preserve Semantics

1. **One arbitration authority.** `USER/Motion/control_arbiter.c:168-215` rejects incoherent/invalid inputs and computes freshness/health centrally.
2. **Manual takeover and safe handover.** `control_arbiter.c:256-313` gives healthy non-zero SBUS immediate authority, revokes external sessions, and requires both sources to remain zero for 150 ms plus a new rearm generation before external control.
3. **Gated reset/re-enable.** `USER/Motion/safety_manager.c:277-339` requires resolved fault causes, zero selection, valid CAN/NMT/heartbeat/status/TPDO, safe device state, and new generation; enable steps remain zero.
4. **Fresh observation before action.** `USER/CanopenApp/canopen_drive_executor.c:229-333` invalidates commands on bus-off/stale observation, times state transitions, and gates approved targets on fresh authorization and mode 3.
5. **Streaming SBUS parser.** `USER/Input/sbus_parser.c:78-132` validates header/footer, resynchronizes after noise, and treats frame-lost/failsafe as unhealthy.
6. **Startup inhibition/fault latch.** `USER/App/startup_coordinator.c:17-138` keeps motion inhibited through boot and communication readiness, latching initialization faults.
7. **Observable runtime.** Metrics, observation structs, mailboxes, and watchdog concepts should become typed snapshots/health checks rather than disappear.
8. **Configuration baselines.** `USER/Config/motion_config.h:7-43` provides initial 150 ms handover, 500 ms quick-stop/disable, 1500 ms heartbeat, and 300 ms TPDO freshness values. These are compatibility defaults subject to Linux measurements/hardware validation.

## Preserve Conceptually, Reimplement

- `control_intent`, arbitration, safety transitions, CiA402 decoding/planning, status freshness, generation tokens, startup sequencing, fault-latch rules, diagnostics schema.
- Pure SBUS byte parser and normalization formulas, after behavior-locking tests.
- CAN frame value validation and error contracts, but not the MCU RX-ring topology.

## Do Not Port

- STM32 HAL, Cube-generated code, FDCAN register/backend code, UART DMA/ISR, TIM6 time source, ThreadX objects/priorities/wrappers, MCU critical sections, BSP pinmux/clock/watchdog/reset code.
- MCU BSP layering as an application hardware framework. Linux kernel SocketCAN, tty, gpiod, systemd, journald, cgroups, and watchdog facilities replace it.
- EasyLogger merely because it existed. Prefer a minimal project logger to stderr/journald unless a later measured need justifies a library.
- Fixed-delay CiA402 state advancement, direct callback-to-actuator paths, or duplicated producer mailboxes.

---

# 3. Proposed Linux Architecture

```text
 ROS2 process/executor                     SBUS UART
        |                                     |
        v                                     v
 ros2_interface adapter             platform/linux/uart
        |                                     |
        v                                     v
 AutonomousCommandSource              input/sbus parser
        \                                     /
         \ immutable timestamped snapshots   /
          +---------------+------------------+
                          |
                          v
                 ControlCycle (single owner)
                          |
              +-----------+-----------+
              |                       |
              v                       v
        ControlArbiter           SafetyManager
              \                       /
               +---- safe intent ----+
                          |
                          v
                    DriveManager
                          |
                    CiA402Drive[]
                          |
              communication/canopen
        (NMT/HB/SDO/PDO/EMCY, node health)
                          |
             CANopenLinux SocketCAN driver
                          |
                    Linux can0/vcan0

 All subsystems -> typed diagnostic snapshots -> logger/journald
                                      \-------> ROS2 diagnostics later
 Process supervisor/signals ----------> lifecycle coordinator
```

### Chosen Boundary

- **Domain:** command, arbitration, safety, drive desired/observed state; no Linux/ROS2/CANopenNode headers.
- **Protocol:** SBUS and CiA402/CANopen application policy; depends on domain types and abstract monotonic time, not devices.
- **Transport/platform:** file descriptors, SocketCAN, tty, timerfd/clock, signals; no motion policy.
- **Orchestration:** owns lifetimes and threads and wires interfaces to domain services.
- **Adapters:** ROS2, CLI commissioning, diagnostics export; never own safety state.

---

# 4. Proposed Repository Layout

```text
robot_control/
├── AGENTS.md
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSES/
├── cmake/
│   ├── modules/
│   └── toolchains/aarch64-rk3588-ubuntu2204.cmake
├── docker/cross/
│   ├── Dockerfile
│   └── image.lock
├── app/
│   ├── robot_control_main.cpp
│   ├── lifecycle_coordinator.{hpp,cpp}
│   └── signal_handler.{hpp,cpp}
├── domain/
│   ├── command/
│   ├── control/
│   ├── safety/
│   ├── drive/
│   ├── diagnostics/
│   └── time/
├── communication/
│   └── canopen/
│       ├── stack/
│       ├── network_manager/
│       ├── cia402/
│       └── profiles/zlac8015d_v4/
├── input/
│   └── sbus/
│       ├── protocol/
│       └── source/
├── platform/
│   └── linux/
│       ├── can/
│       ├── uart/
│       ├── time/
│       ├── process/
│       └── system/
├── ros2_interface/
├── service/
│   ├── config/
│   ├── diagnostics/
│   ├── health/
│   └── logging/
├── config/
│   ├── development/
│   ├── production/
│   └── schema/
├── deploy/
│   ├── systemd/
│   ├── udev/
│   └── tmpfiles.d/
├── scripts/
│   ├── build/
│   ├── sysroot/
│   ├── deploy/
│   └── test/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── fixtures/sbus/
│   ├── vcan/
│   └── hardware/
├── tools/
│   ├── can_probe/
│   ├── drive_commission/
│   └── timing_probe/
├── third_party/
│   ├── CANopenNode/
│   ├── CANopenLinux/
│   └── README.md
└── docs/
    ├── architecture/
    ├── decisions/
    ├── hardware/
    ├── operations/
    └── verification/
```

### Top-Level Responsibilities and Forbidden Dependencies

| Module | Responsibility | Forbidden |
|---|---|---|
| `domain` | Pure typed state, policies, state machines | Linux, ROS2, CANopenNode, filesystem |
| `communication/canopen` | CANopen controller orchestration and project CiA402/profile layer | ROS2, UART, robot app lifecycle |
| `input/sbus` | Pure decoder plus source adapter | CANopen, motion outputs; protocol cannot include Linux |
| `platform/linux` | Thin RAII wrappers for Linux APIs | domain policy, CiA402, source priority |
| `service/config` | parse/validate immutable startup config | device I/O, policy decisions |
| `service/diagnostics` | aggregate immutable observations | actuator control |
| `ros2_interface` | translate typed internal API to/from ROS2 | SocketCAN/tty/CANopenNode/direct drive |
| `app` | composition root, lifecycle, thread ownership | protocol algorithms and duplicated policy |
| `tools` | bounded commissioning/measurement utilities | production automatic motion defaults |
| `deploy` | systemd/udev/packaging assets | source-level board pinmux |
| `third_party` | pinned upstream code | local policy edits without patch provenance |

Correction to the proposed layout: replace vague `common/` with cohesive `domain/`; replace `platform/hal` with `platform/linux`; isolate vendor-specific drive profile; put system assets in `deploy/`; keep commissioning tools separate from the daemon.

---

# 5. Dependency Direction

Allowed compile-time edges:

```text
app -> ros2_interface (optional build)
app -> service -> domain
app -> communication/canopen -> domain
app -> input/sbus/source -> input/sbus/protocol -> domain
app -> platform/linux
communication/canopen/stack -> CANopenNode + CANopenLinux
communication/canopen/cia402 -> domain/drive + stack facade
ros2_interface -> domain command/diagnostic API
tests -> any public target
```

Rules:

- `domain/safety` may consume drive/network/SBUS/command observations expressed as domain types. It must not call transport code.
- `communication/canopen` reports health and accepts typed drive intents; it cannot decide robot command ownership.
- A `SafetyDriveAction` adapter translates robot safety output into CiA402 actions. CiA402 never infers robot safety from a statusword alone.
- No lower layer depends on `app`, `ros2_interface`, or `service`.
- CMake target link interfaces and an include-dependency check enforce this graph.

---

# 6. Execution Model

## Initial Threads

1. **CANopen I/O/event thread:** CANopenLinux/epoll or poll processing, CAN receive, stack deadlines, heartbeat/NMT/SDO/PDO/EMCY. Target wake cadence/deadline: 1 ms where required by the upstream integration, otherwise event/deadline driven.
2. **Control thread:** absolute `CLOCK_MONOTONIC` periodic schedule at initial 100 Hz. It snapshots sources and observations, runs arbiter, safety manager, drive planner, and publishes exactly one immutable drive action set.
3. **SBUS reader thread:** blocking/poll tty input, feeds pure parser, publishes versioned state snapshot.
4. **ROS2 executor thread/process adapter (later):** publishes autonomous command snapshots and reads diagnostic snapshots. Its failure cannot own or stop the other threads.

Diagnostics and health aggregation run as low-rate work (for example 1 Hz/10 Hz) in the app/service loop or CAN/control thread budget, not a dedicated thread unless measurements justify it.

## Mutable State Ownership

- Each producer owns its private mutable decoder/adapter state.
- Cross-thread exchange uses single-writer snapshots with sequence number, monotonic capture time, validity flags, and generation/session ID. Start with mutex-protected copy snapshots for correctness; replace with atomics/double buffering only if measured contention matters.
- The control thread exclusively owns arbiter and robot safety state.
- CANopen thread exclusively owns CANopenNode objects and SDO transaction state.
- Cross-thread drive actions use a bounded latest-value mailbox plus generation/sequence; never an unbounded motion queue.

## Timing

- Initial control rate: **100 Hz / 10 ms** because commanded wheel velocity and safety decisions do not initially justify 1 kHz Linux policy execution. This must be validated against drive RPDO/TPDO behavior and command latency.
- Initial CANopen processing deadline: **1 ms** compatibility baseline from the legacy stack and upstream processing model; actual event loop should sleep until the earliest stack deadline.
- Use absolute sleeps to prevent drift.
- Record p50/p95/p99/max control wake lateness, cycle runtime, snapshot age, action-to-RPDO latency, CAN loop stall, and missed deadline count.
- Phase gate before real-time tuning: on target under representative CAN/ROS2/log load, p99 control wake lateness <2 ms, max <10 ms over 1 hour, and no two consecutive control deadlines missed. If not met, evaluate thread priority/affinity first; PREEMPT_RT only after evidence.

## Blocking/Synchronization

- No SDO wait, filesystem write, DNS, ROS2 operation, or logging format/output blocks the control cycle.
- SDO is asynchronous with timeout/cancellation in CANopen thread.
- No locks are held across syscalls or callbacks.
- Shutdown is a lifecycle state observed by all owners; control first publishes inhibit/zero, CANopen confirms the safe transition or deadline, then teardown proceeds.

---

# 7. CANopen Architecture

## Decision

Use **CANopenNode + CANopenLinux as pinned upstream components**, reuse CANopenLinux's SocketCAN driver/event integration, and build a project-owned controller/network facade plus CiA402 application layer. Do not write a second raw SocketCAN driver for CANopen traffic.

Keep a small project `platform/linux/can` wrapper only for:

- interface presence/state diagnostics,
- optional raw probe/test tooling,
- netlink/read-only status abstraction if needed,
- non-CANopen test injection.

It must not compete with CANopenLinux for the production CAN socket.

## Boundaries

```text
DriveManager
  -> CiA402Drive (desired/observed protocol state)
  -> VendorProfile (object indices, PDO layout, units)
  -> CanopenControllerFacade
      -> NMT manager
      -> HB consumer / boot-up
      -> async SDO client
      -> RPDO/TPDO binding
      -> EMCY observation
      -> CANopenNode
      -> CANopenLinux SocketCAN
```

## Required State

Per CANopen node/drive endpoint:

- configured identity, node ID, expected vendor/product/revision/serial where available;
- NMT desired/actual state, boot generation, last heartbeat, availability;
- SDO transaction state/abort code;
- PDO mapping profile/version, TPDO age, RPDO sequence;
- last EMCY and error register/history;
- raw and decoded statusword(s), controlword, requested/actual mode;
- desired/actual CiA402 state, transition deadline, command/feedback validity;
- target/actual velocity per axis, scaling/limits/direction;
- fault/recovery count and recovery policy.

## Startup Sequence

1. Parse/validate configuration; start inhibited.
2. Open/bind CAN interface; report link state.
3. Initialize stack as NMT manager/controller; discover expected node boot-up/heartbeat.
4. Verify identity/object availability using SDO.
5. Read and validate drive protection, mode behavior, PDO map; configure volatile settings only when policy permits.
6. Enter pre-operational, configure PDOs, verify readback, then NMT operational.
7. Require fresh TPDO/status/mode and zero target.
8. Execute CiA402 shutdown → switch-on → enable-operation using decoded status and timeouts, with zero targets throughout.
9. Request robot safety authorization; only then accept non-zero actions.

## Fault/Recovery

- `can0` disappears/read error: invalidate all node/PDO observations, publish zero/inhibit, attempt bounded reopen with backoff; never retain authorization generation.
- Bus-off: link monitor detects state, safety selects zero then quick-stop/disable policy; privileged network configuration may use `restart-ms`. Application observes recovery and performs full node/drive requalification.
- Heartbeat/TPDO lost: affected node unavailable, zero all coupled motion axes, revoke authorization; require boot/heartbeat/PDO/mode/state requalification.
- EMCY/drive fault: latch decoded/raw evidence, inhibit, apply configured quick-stop/disable, and require gated reset.
- Process restart: start inhibited and repeat full qualification; no persisted live authorization.
- Shutdown: zero target, wait bounded confirmation, quick-stop/disable according to configuration, NMT stop/pre-op if appropriate, close stack.

## Drive-Specific Caution

The existing notes identify one CANopen node controlling two motor axes and document nonstandard 32-bit/dual-half status and subindices (`docs/ZLAC8015D_CANOPEN_NOTES.md:14-24,145-191`). Therefore model:

- one `CanopenNodeEndpoint`,
- one vendor endpoint profile,
- two `Axis` domain objects,
- one explicit mapping record from protocol half/subindex to physical left/right.

Do not assume packed target/status mapping or axis order until unloaded hardware validation.

## Why Not ros2_canopen Core

It would couple drive lifecycle and availability to ROS2 executor/lifecycle and make ROS2 the operational center. It may later serve as reference/interoperability tooling, but the production low-level core must remain a standalone daemon with an internal typed adapter.

---

# 8. Build Architecture

## Host Build

- `host-debug`: all pure libraries, unit tests, sanitizers optional.
- `host-test`: deterministic tests plus `vcan`/PTY integration where Linux privileges permit.
- CANopenLinux may build natively for `vcan`.
- ROS2 is `ROBOT_CONTROL_ENABLE_ROS2=OFF` until its deployment decision is complete.

## Cross Build and Sysroot

Chosen strategy: **versioned sysroot from the actual RK3588 Ubuntu 22.04 image**, normalized and archived with a manifest/checksum; use the existing Ubuntu 22.04 GCC 11 aarch64 cross toolchain. This captures installed and vendor board libraries not guaranteed by the compiler container.

Process:

1. Record target `/etc/os-release`, kernel, `dpkg-query`, architecture, loader, GCC runtime, and relevant device libraries.
2. Read-only sync `/lib`, `/usr/lib`, and required headers from `/usr/include` plus architecture directories, excluding runtime state/secrets.
3. Convert absolute library symlinks and validate linker scripts for relocatable sysroot use.
4. Store no mutable target sysroot in Git; store generation script, allowlist, manifest, archive checksum, and artifact location/version.
5. Toolchain sets `CMAKE_SYSROOT`, compiler target, find-root modes (`ONLY` for library/include/package; `NEVER` for programs), and `PKG_CONFIG_SYSROOT_DIR`/`PKG_CONFIG_LIBDIR`.
6. Automated ELF audit checks `readelf -l/-d`, interpreter, RPATH/RUNPATH, GLIBC/GLIBCXX symbol versions, and resolves dependencies against target/sysroot.
7. Target smoke runs binary/library probes before project feature work.

Alternative: an Ubuntu 22.04 arm64 package-derived sysroot is reproducible from public packages but may miss vendor board libraries. It may support CI; the actual-board sysroot remains the release compatibility authority until the board image is reproducibly generated.

## Docker

- Reuse the owner-provided `rk3588-cross` image and verify its immutable local image ID, compiler, CMake, and target triplet. Archive/reconstruction provenance remains required before distributed release CI.
- Image contains compiler, CMake/Ninja, pkg-config wrapper, test/static-analysis tools; sysroot is mounted read-only.
- Script mounts checkout and build/cache paths, passes current UID/GID or runs `--user $(id -u):$(id -g)`, sets a writable HOME/cache, and never edits source ownership.
- Outputs: `out/build/<preset>/`, `out/artifacts/<version>/`, `out/test-results/`.

## Presets

- `host-debug`, `host-test`
- `rk3588-debug`, `rk3588-release`
- optional `rk3588-native-debug`

CMake targets are per module; architecture differences are confined to adapter targets and toolchain/preset options.

---

# 9. Development Workflow

```text
Codex/developer edits host checkout
  -> host unit tests and static checks
  -> scripts/build/build_rk3588.sh
       -> pinned Docker image
       -> read-only versioned sysroot
       -> out-of-source Ninja build
       -> ELF/ABI audit
  -> scripts/deploy/deploy_rk3588.sh --target <inventory-name> --staging
       -> rsync versioned artifact to /opt/robot-control/releases/<version>
       -> verify checksum
       -> atomically update staging/current only when requested
  -> scripts/deploy/run_rk3588.sh or systemctl restart staging service
  -> scripts/deploy/fetch_logs.sh
  -> host stores logs/results under out/target-results/<run-id>
```

- Target inventory/config lives outside secrets; host names are overridable.
- SSH keys/agent used; no passwords.
- Deploy defaults to dry-run/staging and never changes kernel, DT overlay, CAN bitrate, persistent drive objects, or production symlink without explicit flags.

---

# 10. Deployment Architecture

## Paths

- `/opt/robot-control/releases/<version>/bin/robot-control`
- `/opt/robot-control/releases/<version>/share/`
- `/opt/robot-control/current` atomic symlink
- `/etc/robot-control/robot-control.yaml` production config (root-owned, daemon-readable)
- `/var/lib/robot-control/` bounded state/evidence only; never live authorization
- journald for logs

## systemd

`robot-control.service`:

- `User=robot-control`, `Group=robot-control`;
- starts after local filesystems and relevant device/network setup unit; uses explicit preflight rather than assuming `network-online`;
- `ExecStartPre` validates config/devices and CAN state without enabling motion;
- `Restart=on-failure` with rate limiting;
- `TimeoutStartSec`/`TimeoutStopSec` cover qualification/safe-stop;
- `KillSignal=SIGTERM`, bounded final SIGKILL;
- hardening: `NoNewPrivileges`, restricted filesystem/address families/capability set after empirical validation;
- stdout/stderr to journald with identifier.

## Permissions

- Ordinary PF_CAN raw socket use generally does not require `CAP_NET_RAW` for a normal process once `can0` exists; verify on the target image.
- Interface creation, bitrate, link-up, and restart policy require `CAP_NET_ADMIN`; keep those in a privileged system/network unit, not the daemon.
- UART: udev group/ACL (for example dedicated `robot-control` group), stable symlink by device/path, no broad root access.
- GPIO later: libgpiod device ACL/group; no `/dev/mem`.
- systemd device/cgroup restrictions are added only after enumerating required device nodes.

## Logging/Diagnostics

- Structured key-value text or JSON-lines optional, always timestamp/severity/module/event/context.
- Default info transitions; debug sampled; repeated errors token-bucket throttled with suppressed-count summary.
- Control loop writes counters/snapshots, not logs.
- Same diagnostic model feeds journald locally and ROS2 messages later.

## Production vs Development

- Same binary where possible.
- Separate validated config files: dev may use `vcan0`, PTY, simulated nodes, verbose logs; production requires real devices, strict identity, limits, drive-loss protection, and no commissioning writes.
- Commissioning is a separate tool/mode with explicit confirmation and audit output.

---

# 11. Test Architecture

## Unit

- Fake monotonic clock drives every timeout deterministically.
- SBUS byte-stream parser/normalization/recovery.
- Source snapshot validity, sequence wrap/replay, monotonic timestamp aging.
- Arbiter truth table and property tests for single-authority/zero handover.
- Safety transition table, latches, reset conditions, shutdown.
- CiA402 mask decoder/planner and vendor status halves.
- Config schema/range/cross-field validation.

## Linux Integration

- PTY-based UART tests for partial reads, bursts, disconnect/reopen, noise.
- `vcan0` for frame send/receive, filtering, timestamping, counter/error surfaces.
- CANopenNode/CANopenLinux plus simulated CANopen node or test peer: NMT, boot, heartbeat, async SDO, PDO mapping/exchange, EMCY, timeout/reconnect.
- Multi-process tests kill/restart ROS2 adapter and daemon.

## Hardware Integration

Ordered gates:

1. identify `can0`, driver, bitrate, error counters, UART stable device;
2. passive CAN capture only;
3. NMT/heartbeat/identity SDO;
4. read-only drive object audit;
5. volatile PDO mapping/readback with rollback;
6. zero-target CiA402 transitions on unloaded drive;
7. one-axis low-speed direction mapping;
8. two-axis target/feedback;
9. SBUS manual takeover/recovery;
10. autonomous source and ROS2 restart;
11. disconnect, heartbeat loss, bus-off, drive fault, process restart, shutdown.

Each gate stores config, firmware identity, commands, raw frames, diagnostics, and verdict in `docs/verification/` or external artifacts.

## Fault Injection

- Time jumps are impossible with monotonic clock but delayed scheduling and stale snapshots are injected.
- Drop/delay/reorder simulated heartbeat/TPDO; SDO abort/timeout; EMCY; CAN socket close; interface down/up; parser corruption; SBUS failsafe; ROS2 stale/sequence replay; SIGTERM/SIGKILL; disk/log pressure.

## Long Run

- Initial 4-hour, then 24-hour soak.
- Representative CAN traffic and ROS2 publish rate.
- Acceptance: no unintended non-zero transition, no deadlock, zero silent deadline misses, bounded memory/file descriptors, successful repeated reconnects, and complete diagnostic counters.

---

# 12. Migration Map

| STM32 concept/module | Linux equivalent | Action |
|---|---|---|
| BSP FDCAN/HAL | CANopenLinux SocketCAN + optional thin status wrapper | Replace |
| CAN RX ISR/ring/router | CANopenLinux socket/event loop | Replace topology; preserve validation/errors |
| UART DMA/ISR | `open`/`termios`/poll reader | Replace |
| `sbus_parser` | pure `input/sbus/protocol` | Port behavior with tests |
| TIM6/app monotonic time | `CLOCK_MONOTONIC` injected clock | Replace |
| ThreadX tasks/timers | explicit four-thread Linux model + absolute timers | Redesign |
| ThreadX mailboxes | versioned latest-value snapshots/mailboxes | Reimplement |
| `control_intent` | domain command types | Port conceptually |
| `control_arbiter` | domain control arbiter | Preserve semantics/test boundaries |
| `safety_manager` | domain robot safety FSM | Preserve intent; formalize table |
| `safety_cia402_adapter` | typed SafetyDriveAction mapper | Preserve separation |
| `cia402_drive`/executor | project CiA402 state/planner + CANopen facade | Port behavior, not callbacks |
| CANopen controller/commissioning | network manager + separate commissioning tool | Split production/commissioning |
| runtime metrics/observation | typed atomic snapshots + diagnostics service | Reimplement |
| MCU watchdog | health supervisor + systemd watchdog after validation | Replace |
| reset cause | process/system boot IDs, exit status, journald | Replace |
| EasyLogger/RTT | stderr structured logger + journald | Replace |
| BSP GPIO/pinmux | DT overlay + kernel + libgpiod adapter if needed | Do not port |
| MCU config headers | immutable validated runtime config + few compile constants | Replace |

---

# 13. Implementation Phases

## Phase 0 — Provenance, ADRs, and Behavioral Baseline

- **Objective:** turn the pre-repository into a trustworthy project baseline before code.
- **Files:** initialize/repair Git metadata; `README.md`, `docs/architecture/*`, `docs/decisions/*`, `third_party/README.md`, license inventory, captured legacy behavior tables.
- **Prerequisites:** none; user authorization only if repairing Git history is destructive (otherwise initialize cleanly).
- **Scope:** record CANopenNode/EasyLogger provenance; decide whether EasyLogger is removed; pin CANopenLinux; convert legacy arbiter/safety/CiA402 semantics into state/transition tables and test vectors; inventory drive docs contradictions; record the selected Ubuntu 22.04 OS baseline.
- **Validation:** every dependency has upstream URL/version/commit/license; every preserved safety rule cites legacy evidence; no product implementation.
- **Acceptance:** approved ADR set and behavior baseline; Git status works; open hardware assumptions are explicit.
- **Risks:** accidental attribution of snapshots; mitigate with hashes and upstream comparison.

## Phase 1 — Reproducible Build and ABI Proof

- **Objective:** prove host tests and Ubuntu 22.04 aarch64 hello/probe artifacts.
- **Files:** root CMake, presets, toolchain, `docker/cross`, sysroot scripts, build scripts, ABI audit.
- **Prerequisites:** Phase 0 dependency decisions; SSH read access to target/rootfs.
- **Scope:** sysroot manifest/sync, pinned Docker image, UID/GID, out-of-source outputs, native/cross/native-target presets.
- **Validation:** host and cross configure; target executes probe; `readelf`/symbol audit passes.
- **Acceptance:** one reproducible command emits host-owned target artifacts that run on board with no unresolved/newer GLIBC dependency.
- **Risks:** incomplete sysroot/linker scripts; mitigate with allowlist, symlink normalization, target smoke.

## Phase 2 — Pure Domain Kernel and Behavior-Lock Tests

- **Objective:** implement types, clock abstraction, command snapshot, arbitration and safety transition skeletons without devices.
- **Files:** `domain/*`, `tests/unit/*`.
- **Prerequisites:** Phase 1 host test build; Phase 0 tables.
- **Scope:** immutable samples, generations, validity, typed safety inputs/actions, legacy compatibility tests.
- **Validation:** deterministic fake-clock table/property tests.
- **Acceptance:** no Linux/CANopen/ROS2 includes; all documented transition cases pass; single final authority invariant proven by tests.
- **Risks:** ambiguous legacy edge cases; keep compatibility tests and flag deviations via ADR.

## Phase 3 — Linux Platform Adapters and Observability Base

- **Objective:** reliable RAII wrappers for monotonic timers, signals, tty, and process diagnostics.
- **Files:** `platform/linux/*`, `service/logging`, Linux integration tests.
- **Prerequisites:** Phase 1.
- **Scope:** fd lifetime, errno-rich statuses, poll/epoll primitive, absolute timers, SIGTERM event, PTY tests, throttled logger.
- **Validation:** leak/error/interrupt/disconnect tests, sanitizer run.
- **Acceptance:** every syscall failure is surfaced; no policy in platform layer; clean cancellation.
- **Risks:** signal/thread races; use signalfd or async-signal-safe wake mechanism.

## Phase 4 — SocketCAN and `vcan` Foundation

- **Objective:** validate target and host CAN mechanics before CANopen integration.
- **Files:** `platform/linux/can`, `tools/can_probe`, `tests/vcan`, target runbook.
- **Prerequisites:** Phases 1/3.
- **Scope:** interface bind, Classic CAN validation, filters, timestamps/counters/link observations; no motion.
- **Validation:** bidirectional vcan and passive target capture; interface-down/reopen tests.
- **Acceptance:** target can passively receive expected frames; error/link state observable; no root needed for normal data path.
- **Risks:** kernel driver limitations; capture driver/netlink evidence.

## Phase 5 — Pinned CANopenNode/CANopenLinux Controller Bring-Up

- **Objective:** integrate upstream stack as controller with async operations.
- **Files:** `third_party/*`, `communication/canopen/stack`, network manager, simulated-node tests.
- **Prerequisites:** Phases 0/4.
- **Scope:** NMT, boot-up, heartbeat consumer, SDO client, PDO, EMCY, deadlines, node availability, reopen/recovery.
- **Validation:** vcan simulated node exercises all protocols and injected failures.
- **Acceptance:** no ROS2; no CiA402 motion; deterministic node health and recovery; upstream patches isolated/documented.
- **Risks:** CANopenLinux controller feature gaps/version mismatch; spike first and upstream minimal patches.

## Phase 6 — Vendor Profile and Read-Only Hardware Qualification

- **Objective:** establish exact ZLAC8015D identity, axes, object types, units, heartbeat and PDO facts without motion.
- **Files:** vendor profile, commissioning read-only tool, hardware records.
- **Prerequisites:** Phase 5 and authorized hardware access.
- **Scope:** identity SDO, heartbeat units, 0x200F, statusword width/halves, current PDO map, protection settings, firmware revision.
- **Validation:** SDO readback/CAN analyzer, compare both PDFs/notes.
- **Acceptance:** no unresolved type/axis/unit assumption needed for first zero-motion CiA402 phase; evidence archived.
- **Risks:** manufacturer contradictions; never guess, block motion gate.

## Phase 7 — CiA402 Single-Endpoint/Two-Axis Zero-Motion Bring-Up

- **Objective:** explicit state decoder/planner and safe enable/disable/reset with zero targets.
- **Files:** `communication/canopen/cia402`, vendor profile, domain drive types/tests.
- **Prerequisites:** Phases 2/5/6.
- **Scope:** 0x6040/6041, 0x6060/6061, desired/actual state, timeouts, faults, quick stop, shutdown; no non-zero command initially.
- **Validation:** exhaustive unit states, vcan simulator, unloaded target zero-transition test.
- **Acceptance:** every transition status-driven and bounded; stale/mode mismatch inhibits; fault reset gated.
- **Risks:** vendor nonconformance; retain raw status and profile-specific decoder.

## Phase 8 — PDO Motion Pipeline and Drive Manager

- **Objective:** one endpoint/two axes, later N endpoints, receive feedback and transmit bounded velocity.
- **Files:** PDO profile/bindings, `domain/drive`, drive manager, HIL tests.
- **Prerequisites:** Phase 7.
- **Scope:** verified mapping, scaling, limit/direction, action generation, feedback age, coupled-failure policy.
- **Validation:** simulator then unloaded low-speed one-axis direction verification and emergency stop.
- **Acceptance:** command-to-RPDO latency measured; unexpected/stale feedback zeros both coupled axes; no duplicated per-drive logic.
- **Risks:** axis inversion/packed mapping; low-speed staged physical evidence.

## Phase 9 — Linux SBUS Source

- **Objective:** independently tested stream decoder and robust tty lifecycle.
- **Files:** `input/sbus/protocol`, `input/sbus/source`, fixtures/tests.
- **Prerequisites:** Phases 2/3.
- **Scope:** frame parser, channel normalization, lost/failsafe/stale, run/stop/gear config, disconnect/reopen, neutral recovery.
- **Validation:** legacy and recorded streams, PTY fragmentation/noise, real receiver passive test.
- **Acceptance:** no termios in parser; recovery cannot publish non-zero valid intent until neutral gate.
- **Risks:** UART inversion/framing/kernel support; validate actual UART/receiver electrical path early.

## Phase 10 — Integrated Control Cycle and Safety Actions

- **Objective:** connect SBUS/autonomous samples, arbiter, safety, drive manager under single-owner 100 Hz loop.
- **Files:** control runtime, lifecycle, safety-drive adapter, integration tests.
- **Prerequisites:** Phases 2/8/9.
- **Scope:** startup inhibit, normal/zero hold/lost/recovering/CAN unavailable/drive fault/e-stop/degraded/shutdown; generations, zero transition.
- **Validation:** scenario matrix with fake clock/simulated CAN/SBUS; timing instrumentation.
- **Acceptance:** no producer can bypass loop; all loss paths meet configured response; handover boundary tests pass.
- **Risks:** coupled state-machine complexity; explicit transition table and trace replay.

## Phase 11 — Diagnostics and Health Supervision

- **Objective:** actionable local diagnostics without control-loop logging pressure.
- **Files:** domain diagnostics, health service, logger, metrics export.
- **Prerequisites:** Phase 10.
- **Scope:** required CAN/CANopen/CiA402/SBUS/control/system counters, snapshots, throttling, watchdog eligibility.
- **Validation:** fault injection yields expected event/counter/reason; log rate bounded.
- **Acceptance:** every safety transition has machine-readable reason and relevant raw ages/status.
- **Risks:** high cardinality/rate; fixed schema and throttling.

## Phase 12 — Service Lifecycle, Permissions, Deployment

- **Objective:** safe non-root daemon under systemd and repeatable staging deployment.
- **Files:** deploy assets, scripts, operations docs.
- **Prerequisites:** Phases 10/11.
- **Scope:** user/groups/udev, CAN preconfiguration, release paths, atomic switch/rollback, SIGTERM safe sequence, restart limiting.
- **Validation:** fresh-target install, restart, SIGTERM/SIGKILL, target reboot, permission-denied tests.
- **Acceptance:** no SSH-session dependency; normal daemon has no CAP_NET_ADMIN; restart never causes motion.
- **Risks:** shutdown deadline vs drive response; drive-side loss protection and conservative timeout.

## Phase 13 — ROS2 Compatibility Investigation and Adapter

- **Objective:** choose deployable ROS2 path only after standalone core is stable.
- **Files:** ROS2 ADR, interface messages, adapter target/package, restart tests.
- **Prerequisites:** target package/container compatibility evidence and Phase 12.
- **Scope:** command subscription, state/diagnostics/safety/source/health publication; monotonic receipt time and command sequence; separate process preferred initially.
- **Validation:** kill/restart/hang ROS2 while CANopen core runs; stale command zero/inhibit; ABI/deployment audit.
- **Acceptance:** ROS2 absence does not restart or corrupt core; no callback controls drive directly.
- **Risks:** ROS2 package/DDS compatibility with the board image must still be verified on target.

## Phase 14 — Fault Injection, Timing Qualification, and Real-Time Decision

- **Objective:** determine whether standard kernel scheduling is sufficient and validate fault policy.
- **Files:** timing/fault tools, test matrix/results, scheduling ADR.
- **Prerequisites:** Phases 10-13.
- **Scope:** load, CAN saturation, reconnects, bus-off, UART loss, ROS2 loss, process restart; priority/affinity experiments only if baseline fails.
- **Validation:** 1-hour timing and 4/24-hour soak, recorded p99/max.
- **Acceptance:** defined timing and recovery thresholds pass or a measured PREEMPT_RT/affinity remediation plan is approved.
- **Risks:** vendor kernel instrumentation/RT limits; maintain fallback image/test.

## Phase 15 — Release Packaging and CI/HIL

- **Objective:** reproducible signed/checksummed release and automated gates.
- **Files:** CI, package manifest, SBOM/license report, HIL runner, release/rollback docs.
- **Prerequisites:** all phases.
- **Scope:** host tests/static analysis/cross build/ABI audit in CI; controlled target/HIL lane; versioned config migrations.
- **Validation:** rebuild equivalence, clean-device install/rollback, CI failure injection.
- **Acceptance:** release provenance, artifacts, config, licenses, tests and HIL evidence are traceable.
- **Risks:** hardware CI flakiness; exclusive fixture locking, power control, artifact capture.

---

# 14. Risk Register

| Risk | Impact | Mitigation / Gate |
|---|---|---|
| Compiler container diverges from board userspace | target binary fails | actual Ubuntu 22.04 board sysroot, linker find-root, ELF/symbol audit, target smoke in Phase 1 |
| Incomplete/vendor sysroot | hidden link/runtime mismatch | manifest/allowlist, normalized symlinks, target package inventory |
| CAN timing/jitter | stale control or drive stop | separate 1 ms/event CAN processing from 100 Hz policy, measure action latency |
| CANopenLinux integration mismatch | custom fork/instability | pinned upstream spike, minimal isolated patches, vcan simulated node |
| Vendor CiA402 deviations | unsafe state decisions | raw values retained, profile decoder, read-only qualification and unloaded gates |
| Linux scheduling latency | missed safety/control deadline | measure first; priority/affinity; PREEMPT_RT only with evidence |
| ROS2/DDS runtime differs from the Ubuntu board image | deployment blocked/core coupling | delay adapter, separate process, verify Tier-1 arm64 packages and target behavior |
| Excess privileges | security/reliability | dedicated user, privileged interface setup unit, udev ACLs, capability audit |
| SBUS UART inversion/framing/latency | manual control unavailable/false recovery | early passive electrical/device test, PTY fixtures, neutral recovery gate |
| Unsafe SIGKILL/power loss | no application cleanup | drive communication-loss timeout, zero-refresh policy, independent e-stop |
| systemd restart loop | repeated bus/drive disturbance | rate limiting, startup inhibit, full requalification |
| RK3588 5.10 vendor driver limitations | missing counters/recovery/timestamp | Phase 4 target probe, record driver features, avoid assumed netlink support |
| CAN bus-off recovery privilege | daemon cannot recover link | configure restart-ms/boot unit with CAP_NET_ADMIN; daemon requalifies only |
| Configuration error | wrong node/limits/device | schema, range/cross-field validation, production identity checks |
| Dual-axis coupled node ambiguity | wrong motor motion | explicit endpoint/axis model and unloaded direction mapping |
| Unpinned snapshots / invalid Git | unreproducible build | Phase 0 provenance and repository repair before implementation |

---

# 15. Open Questions

Only questions requiring target/business/hardware evidence remain:

1. **Exact target rootfs access and update policy.** Is SSH root/read access available for sysroot generation, and is the board image fixed for releases? This determines whether the board-derived sysroot can be the release authority.
2. **ROS2 distribution and algorithm-side contract.** Which ROS2 distribution currently runs or is mandated, and where does it run (same Debian host, container, or remote machine)? This blocks only Phase 13 message ABI/deployment choice, not the core.
3. **Physical SBUS interface.** Which UART device/overlay, electrical inverter/interface, baud/framing behavior, and receiver channel mapping are installed? This affects Phase 9 hardware adapter/config.
4. **Emergency-stop hardware path.** Is there an independent physical e-stop wired to the drive, GPIO, safety relay, or only SBUS/software? This changes the shutdown/emergency policy and safety claims.
5. **Motor/axis count and coupling.** Is production limited to the one dual-axis ZLAC8015D node, or will multiple nodes/drives be required in the first release? The architecture supports N endpoints, but Phase 8 acceptance scope depends on this.
6. **Authorized automatic recovery policy.** After CAN/drive faults, may communication recover automatically while motion remains gated, and who provides manual rearm (SBUS switch, ROS2 service, physical input)? This affects the safety transition table.
7. **Drive firmware/hardware identity.** Exact revision and whether persistent communication-loss protection/PDO parameters may be configured during commissioning. This blocks safe motion qualification.
8. **Initial latency requirement.** The plan proposes 100 Hz with p99 wake lateness <2 ms and max <10 ms. If the robot dynamics require a stricter command-to-actuator bound, control rate and kernel evaluation change.
9. **Ubuntu image maintenance path.** Identify the vendor/image update owner and security-update policy for the flashed Ubuntu 22.04 image; this affects production patching and kernel/device compatibility.

These do not block Phase 0 except rootfs availability is needed early in Phase 1.

---

# 16. Recommended First Implementation Step

**Phase 0 / Step 1: create a read-only baseline/provenance manifest and behavioral contract before adding build or runtime code.**

Specifically, the next agent should:

1. repair/initialize Git safely without discarding existing files;
2. hash and identify the exact upstream revisions/licenses of `components/CANopenNode` and `components/EasyLogger`;
3. select and pin a CANopenLinux revision compatible with the chosen CANopenNode revision;
4. maintain ADR-0001 through ADR-0005 as the historical planning record and ADR-0006 as the accepted Ubuntu 22.04 target baseline;
5. transcribe the cited legacy arbitration/safety/CiA402 rules into explicit transition tables and test-vector specifications;
6. review these documents before any product C/C++ implementation.

**Do not execute this step as part of planning.**

---

# External Evidence Used

- [CANopenNode device support](https://canopennode.github.io/CANopenNode/md_doc_2deviceSupport.html) identifies CANopenLinux as the Linux/SocketCAN integration rather than requiring a new project driver.
- [CANopenLinux SocketCAN interface](https://canopennode.github.io/CANopenLinux/group__CO__socketCAN.html) and [application interface](https://canopennode.github.io/CANopenLinux/group__CO__applicationLinux.html) support separating real-time/deadline processing from mainline application work.
- [Linux SocketCAN documentation](https://docs.kernel.org/networking/can.html) defines the PF_CAN/CAN_RAW userspace model.
- [CMake `CMAKE_SYSROOT`](https://cmake.org/cmake/help/latest/variable/CMAKE_SYSROOT.html) confirms compiler and find-path sysroot behavior.
- [Debian Bullseye release information](https://www.debian.org/releases/bullseye/) and [LTS announcement](https://www.debian.org/News/2024/20240814) establish arm64 support and the 2026-08-31 LTS end date.
- [REP-2000](https://www.ros.org/reps/rep-2000.html) classifies ROS2 Humble on Debian Bullseye arm64 as Tier 3/source-build, while Ubuntu 22.04 arm64 is the supported Tier-1 path.
- [systemd.exec](https://www.man7.org/linux/man-pages/man5/systemd.exec.5.html), [udev](https://manpages.debian.org/bookworm/udev/udev.7.en.html), and the [GPIO character-device API](https://docs.kernel.org/userspace-api/gpio/chardev.html) support least-privilege service, stable device permissions, and libgpiod/chardev choices.

---

# Project-Level AGENTS.md Summary

The root `AGENTS.md` created alongside this plan encodes:

- the standalone low-level middleware mission;
- dependency direction and forbidden cross-layer dependencies;
- safety invariants, command generations, zero/recovery/shutdown behavior;
- single-owner concurrency and monotonic-time rules;
- Ubuntu 22.04 board-derived sysroot/Docker/dependency pinning rules;
- Doxygen/error/ownership/logging standards;
- test gates and hardware safety constraints;
- non-root deployment/permissions and agent workflow.

It deliberately avoids phase-specific implementation detail and points agents to this plan as the authoritative staged roadmap.

---

# Verification Steps for This Plan

- Repository and legacy paths were inspected read-only.
- Every current-repository claim references an existing file/line or an observed absence.
- Legacy behavior claims cite concrete implementation ranges.
- Every implementation phase has objective, files, prerequisites, scope, validation, acceptance, and risks.
- No product code, hardware configuration, deployment, motor action, or persistent drive change was performed.
