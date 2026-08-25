# P5.3 CANopen Lifecycle Implementation Baseline

Date: 2026-08-25

Implementation revision: `889459ad1882bcbce5b00b44c371e6d275ab7bb8`

## Outcome

P5.3 now has a project-owned, single-thread CANopen lifecycle facade around
the pinned CANopenLinux epoll driver. It opens only a named interface that
already exists, never configures the link, and keeps the P5.3 default-deny
transmit boundary in every normal build.

`Lifecycle::create()` performs the reviewed upstream initialization order:

1. claim and allocate `StackStorage`;
2. create the upstream monotonic epoll/timer/event descriptors;
3. register a borrowed process-lifetime `TerminationEvent`;
4. resolve the existing interface with `if_nametoindex()`;
5. call `CO_CANinit()`, `CO_CANopenInit()`, `CO_CANopenInitPDO()`, and
   `CO_CANsetNormalMode()` from the sole owner;
6. report interface, controller node, remote node, upstream error, OD error
   information, and captured `errno` on failure.

`Lifecycle::run_until()` uses the upstream `CLOCK_MONOTONIC` timer interval
at 1 ms, consumes SIGINT/SIGTERM through the existing synchronous signalfd,
processes CAN receive/RPDO and mainline functions in one context, handles
`CO_RESET_COMM` through explicit reinitialization, and performs one bounded
reopen attempt on endpoint `EPOLLERR`/`EPOLLHUP`. Application-reset and quit
requests return typed exit reasons; no reboot or link configuration occurs.

The termination descriptor remains owned by process composition. This was a
review correction: transferring it into a lifecycle that later failed startup
could close the only signalfd while SIGINT/SIGTERM remained blocked. Borrowing
the process-lifetime event preserves the termination path across any CANopen
startup failure.

`StackStorage::prepare_communication_reset()` closes any upstream endpoint,
clears every `OD_entry.extension`, restores generated mutable OD storage, and
reapplies the injected receive-only configuration before every initial open,
communication reset, reopen, or failed-init cleanup.

## Host contract

The CANopen host contract uses a deliberately missing interface name, so it
does not create a CAN socket. It proves that startup failure:

- reports `if_nametoindex`, interface and both node identities,
  `upstream=not-called`, and `ENODEV`;
- releases epoll, timerfd, and eventfd without changing the caller-owned
  termination descriptor count;
- clears all OD extensions; and
- releases the process-wide stack claim for immediate reacquisition.

The existing direct `CO_CANsend()` and initial `CO_NMT_process()` contracts
continue to return `CO_ERROR_SYSCALL` with `errno=EACCES`. A reset-specific
contract installs an OD extension, calls `prepare_communication_reset()`, and
proves that extensions, interfaces, and injected heartbeat configuration are
restored.

Fresh host commands:

```bash
rtk cmake -S . -B /tmp/robot_control_p5_3_final_debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON
rtk cmake --build /tmp/robot_control_p5_3_final_debug --parallel 2
rtk ctest --test-dir /tmp/robot_control_p5_3_final_debug \
  --output-on-failure \
  -E '^socketcan_(socket_lifecycle|vcan_managed)$'

rtk cmake -S . -B /tmp/robot_control_p5_3_final_release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON
rtk cmake --build /tmp/robot_control_p5_3_final_release --parallel 2
rtk ctest --test-dir /tmp/robot_control_p5_3_final_release \
  --output-on-failure \
  -E '^socketcan_(socket_lifecycle|vcan_managed)$'
```

Both configurations exited 0, built 52/52 steps, and passed 18/18 selected
tests. `socketcan_socket_lifecycle` and `socketcan_vcan_managed` were explicitly
excluded to preserve this run's zero-CAN/zero-vcan boundary.

## Transmit and static checks

```bash
rtk git diff --check
rtk nm -u \
  out/build/host-debug/communication/canopen/CMakeFiles/robot_control_canopen_upstream.dir/__/__/components/CANopenLinux/CO_driver.c.o
```

Both commands exited 0. The driver object has no undefined libc `send` symbol;
its only transmit-site dependency is
`robot_control_canopen_deny_transmit`. The lifecycle calls no transmit API
directly.

The host compiler warning policy passed with `-Werror`. This machine does not
provide `clang-format`, `clang-tidy`, or `cppcheck`. A supplemental GCC 13.3
`-fanalyzer` build was attempted, but exited 1 on pre-existing analyzer false
positives in `platform/linux/error.hpp` before reaching the changed lifecycle
object; it is not claimed as a passed static-analysis gate.

## RK3588 cross evidence

Both commands used the locked Docker image with network disabled and a clean
source snapshot:

```bash
rtk env \
  ROBOT_CONTROL_SYSROOT=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204.lock.json \
  ROBOT_CONTROL_PRESET=rk3588-debug \
  ./scripts/build/build_rk3588.sh

rtk env \
  ROBOT_CONTROL_SYSROOT=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/locks/rk3588-ubuntu2204-a685ab13.json \
  ROBOT_CONTROL_PRESET=rk3588-release \
  ./scripts/build/build_rk3588.sh
```

Debug and Release each exited 0, completed 41/41 steps, and passed interpreter,
needed-library, symbol-version, Phase 3 symbol, and no-RPATH audits. Both
metadata files record `dirty: false`, 655 source files, revision
`889459ad1882bcbce5b00b44c371e6d275ab7bb8`, and snapshot SHA-256
`c7f6817221907eb75396f7fc1aef23fda94e8418deac47e303645cec94705479`.

| Preset | Artifact / metadata | SHA-256 |
|---|---|---|
| Debug | `out/artifacts/rk3588-debug/robot-control-platform-probe` | `60467297cc86e281af7a48774c95bb4c8d7f4819e8afa63def1f557ae23b65e6` |
| Debug | `out/artifacts/rk3588-debug/build-metadata.json` | `d517e0f9e978e650a07819b87760df3dd494f3735b34f1c5c21f09e8037bdec6` |
| Debug | `librobot_control_canopen_upstream.a` | `bd08d440a9b4aa4abb91742edf0cde2f3ba2c247ebfd666948d166fb84be7a79` |
| Debug | `librobot_control_communication_canopen.a` | `42d1f0e61900daff15c30e68fd5d94a02bb1729f2a386e88bd96169aedf6f957` |
| Release | `out/artifacts/rk3588-release/robot-control-platform-probe` | `35ad27e6aad21e4c5dbbe384b597b7a1868e07739b4627872c94653e5f03389a` |
| Release | `out/artifacts/rk3588-release/build-metadata.json` | `c764af5b523e4c515e413ffb56a84ccdd5cbeb1676c3dbb203e9cda6c84a2068` |
| Release | `librobot_control_canopen_upstream.a` | `35754c2d9c2c294055e6d7d735b669602db9699120732cf1c0a3ad06196e443c` |
| Release | `librobot_control_communication_canopen.a` | `bfdac18f5a6833080ad28e0e034963783534f75b24ba83ac4c3cd7d5f4c5ea84` |

## Review result

```yaml
review_result:
  schema_version: 1
  source_revision: 889459ad1882bcbce5b00b44c371e6d275ab7bb8
  scope:
    - communication/canopen/lifecycle.hpp
    - communication/canopen/lifecycle.cpp
    - communication/canopen/stack_storage.hpp
    - communication/canopen/stack_storage.cpp
    - communication/canopen/CMakeLists.txt
    - tests/unit/canopen_stack_tests.cpp
  toolchain_context: GNU 13.3 host C++20; GNU 11.4 aarch64 C++20
  findings: []
  verification:
    - fresh Host Debug 52/52 build and 18/18 zero-CAN tests
    - fresh Host Release 52/52 build and 18/18 zero-CAN tests
    - default-deny transmit symbol audit
    - RK3588 Debug and Release 41/41 plus ELF audits
```

## Deferred runtime evidence

No CAN socket, `vcan`, physical CAN, RK3588 target, deployment, link
configuration, drive write, or motion operation was performed. Therefore this
baseline does not claim runtime passage for deadline/SIGINT/SIGTERM while an
endpoint is open, live interface loss, or successful endpoint reopen. Those
paths are implemented here and remain assigned to the managed-vcan lifecycle
scenarios in P5.5 and the passive target evidence in P5.7.
