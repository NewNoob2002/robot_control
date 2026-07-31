# ADR-0002: CANopen and Logging Dependencies

- Status: Amended
- Date: 2026-07-31

## Context

The repository contains unversioned snapshots of CANopenNode and EasyLogger but
no Linux CANopen transport. CANopenNode upstream maintains CANopenLinux as its
SocketCAN integration. The low-level core must be independent of ROS2.

## Decision

Use the compatible pair recorded in `third_party/README.md`:

- CANopenLinux `f1348d4072cdabea4c3435a13c721ac29ab4cc91`;
- its pinned CANopenNode submodule
  `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`.

Use CANopenLinux's SocketCAN/epoll integration rather than implementing a
parallel production transport. Add a project-owned CANopen controller facade,
network manager, CiA402 layer, and vendor drive profile above it.

Do not use ros2_canopen as the low-level core.

The original logging decision was amended after Phase 3 review: use the
repository's EasyLogger core through a project-owned Linux port and C++ facade.
Keep synchronous stderr output for journald, disable color/async/buffer modes,
retain project-owned structured fields and repetition throttling, and expose
port failures through atomic health counters without recursively logging them.

## Alternatives

- Custom SocketCAN driver for CANopenNode: rejected as duplicated maintenance
  unless the integration spike proves a concrete upstream limitation.
- ros2_canopen core: rejected because it couples safety availability to ROS2.
- Direct use of EasyLogger macros throughout the application: rejected because
  it would spread global state and third-party APIs across domain modules.
- FetchContent from `master`: rejected because normal builds must be reproducible
  and offline.

## Consequences

- The upstream dependency pair has a known API relationship and Apache-2.0
  licensing.
- Upstream stack objects remain owned by the CANopen execution context.
- Project policy is isolated from upstream callbacks.
- Any upstream patch requires provenance and review.
- EasyLogger's process-global state is confined to `service/logging`; this is a
  documented exception to the no-global-state preference imposed by the chosen
  library. Domain and platform APIs do not depend on EasyLogger headers.

## Verification

- Dependency checkout matches both immutable commits.
- Release builds perform no network fetch.
- A vcan integration spike proves NMT, heartbeat, SDO, PDO, EMCY, shutdown, and
  reopen behavior before CiA402 motion work.
