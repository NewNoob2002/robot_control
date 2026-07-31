# ADR-0001: Layer and Dependency Boundaries

- Status: Accepted
- Date: 2026-07-31

## Context

The STM32 system combined portable control policy with ThreadX, HAL, FDCAN,
UART DMA, and MCU timing. Reproducing an MCU BSP in Linux would duplicate
kernel responsibilities and entangle safety policy with device mechanisms.

## Decision

Use the following dependency direction:

```text
app and adapters
  -> services and orchestration
  -> domain control/safety/drive policy
  -> protocol adapters
  -> platform/linux
  -> Linux APIs and kernel
```

The domain layer contains no Linux, ROS2, CANopenNode, filesystem, or device
headers. `platform/linux` contains mechanisms only and cannot choose command
authority or safety actions. ROS2 cannot access CANopen or devices directly.
CiA402 protocol state and robot-level safety are separate state machines joined
by a typed adapter.

## Alternatives

- Port the STM32 BSP/RTOS layout: rejected because Linux already owns drivers,
  scheduling primitives, permissions, logging, and device configuration.
- Make ROS2/ros2_canopen the core: rejected because ROS2 restart or executor
  failure must not own low-level safety coherence.
- One monolithic daemon module: rejected because it prevents host testing and
  creates circular policy/device dependencies.

## Consequences

- Pure control behavior can be tested on x86_64.
- More explicit typed boundaries and adapter code are required.
- CMake targets and include checks must enforce the dependency graph.
- Device-tree and pinmux remain outside application source.

## Verification

- Every module target declares only allowed dependencies.
- A dependency-graph check fails on reverse edges.
- Domain tests build without Linux device or ROS2 dependencies.

