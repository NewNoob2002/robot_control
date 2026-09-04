# P5.4 Immutable CANopen Observation Baseline

Date: 2026-08-26

Implementation revision: `05a948660596ecab3ec2f4bf487976b0cb60865b`

## Outcome

P5.4 adds a project-owned observation state machine to the P5.3 single-owner
lifecycle. Readers receive copied `ObservationSnapshot` values and never access
live upstream objects. One mutex protects the short state copy/update boundary;
there is no new thread, queue, callback hierarchy, transport, or per-frame heap
allocation.

The snapshot retains raw identifier, DLC, all eight payload bytes, owner
`steady_clock` time, transport/boot generation, and protocol values for remote
NMT/heartbeat, EMCY, and four TPDO slots. The SDO result slot remains empty in
normal P5.4 operation and is reserved for the explicitly authorized P5.6
commissioning request generation.

`StackConfig` now injects and validates one TPDO freshness timeout plus four
expected DLC values. The reviewed passive fixture inventory remains TPDO1 DLC 8
and TPDO2..4 DLC 0; no payload half is assigned to a physical axis.

## Receive and lifecycle contract

For one CAN `EPOLLIN` event, the owner captures one monotonic timestamp, peeks
one `can_frame` from the existing upstream descriptor with
`MSG_PEEK | MSG_DONTWAIT`, and then calls `CO_CANrxFromEpoll()` exactly once. A
matched frame must equal the peeked frame. The owner clears `epoll_new` before
calling `CO_epoll_processRT()`, so the pinned stack remains the sole consumer and
the frame is not received twice.

The pinned driver only configures `CAN_RAW_ERR_FILTER` when its optional error
reporting module is enabled. That module is intentionally disabled because it
contains link-reset policy. P5.4 instead sets `CAN_ERR_MASK` on the already-open
upstream socket from project-owned lifecycle code. This requests raw error
frames without enabling upstream link configuration or creating another socket.

Initial open, communication reset, endpoint reopen, and a received CAN error
advance the transport generation and invalidate all remote records. A valid
one-byte boot-up advances the boot generation. Heartbeat/NMT and TPDO records
become current only in that boot generation; malformed, future, and replayed
records remain diagnostic and cannot refresh feedback.

The owner calls `advance_time()` each event-loop iteration. The exact timeout
boundary remains current; the first later monotonic instant invalidates the
record and advances snapshot version once. `snapshot(now)` additionally refuses
to present a future or expired record as current if a reader races the next
owner timer tick.

## Test-first evidence

The initial test-only build deliberately exited 1 because
`communication/canopen/observation.hpp` did not yet exist:

```bash
rtk cmake -S . -B /tmp/robot_control_p54_red -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON
rtk cmake --build /tmp/robot_control_p54_red \
  --target robot_control_canopen_stack_tests --parallel 2
```

The green host contract covers:

- invalid TPDO timeout and DLC configuration;
- heartbeat before boot and fresh boot generation;
- NMT state, TPDO1 DLC 8, and TPDO2 DLC 0;
- exact TPDO/heartbeat deadline and first-later-instant invalidation;
- version changes caused by owner timeout transitions;
- malformed TPDO and EMCY DLC;
- little-endian EMCY protocol fields;
- future timestamp rejection;
- old and mixed-generation replay rejection;
- CAN error, reopen, and boot-required invalidation; and
- immutable value-copy behavior and empty normal SDO result.

## Host Debug and Release

Each command exited 0. Both configurations built 53/53 steps and passed 18/18
tests while excluding the two SocketCAN runtime tests:

```bash
rtk cmake -S . -B /tmp/robot_control_p54_final_debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON
rtk cmake --build /tmp/robot_control_p54_final_debug --parallel 2
rtk ctest --test-dir /tmp/robot_control_p54_final_debug \
  --output-on-failure \
  -E '^socketcan_(socket_lifecycle|vcan_managed)$'

rtk cmake -S . -B /tmp/robot_control_p54_final_release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON
rtk cmake --build /tmp/robot_control_p54_final_release --parallel 2
rtk ctest --test-dir /tmp/robot_control_p54_final_release \
  --output-on-failure \
  -E '^socketcan_(socket_lifecycle|vcan_managed)$'
```

## Sanitizer and static checks

The sanitizer target built successfully with GCC 13.3 AddressSanitizer and
UndefinedBehaviorSanitizer. Its first CTest invocation exited 8 because
LeakSanitizer cannot run under the execution environment's ptrace supervision;
this was classified as an infrastructure limitation, not retried as a product
failure. The same binary passed after disabling only leak detection:

```bash
rtk cmake -S . -B /tmp/robot_control_p54_sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
rtk cmake --build /tmp/robot_control_p54_sanitize \
  --target robot_control_canopen_stack_tests --parallel 2
rtk env ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir /tmp/robot_control_p54_sanitize \
  -R '^canopen_stack_build$' --output-on-failure
```

LLVM 22.1.8 format and scoped tidy checks exited 0 for `observation.cpp`,
`lifecycle.cpp`, `stack_config.cpp`, and `canopen_stack_tests.cpp`.
`git diff --check` also exited 0:

```bash
rtk /opt/llvm-22.1.8/bin/clang-format --dry-run --Werror \
  communication/canopen/observation.hpp \
  communication/canopen/observation.cpp \
  communication/canopen/lifecycle.hpp \
  communication/canopen/lifecycle.cpp

rtk /opt/llvm-22.1.8/bin/clang-tidy \
  communication/canopen/observation.cpp \
  -p /tmp/robot_control_p54_final_debug --quiet \
  --checks='-*,clang-analyzer-*,bugprone-*,-bugprone-unchecked-optional-access,performance-*,portability-*,-portability-avoid-pragma-once' \
  --warnings-as-errors='clang-analyzer-*,bugprone-*'
```

The same tidy policy passed for the other three files. The pinned driver object
still has no undefined libc `send`; it resolves transmit attempts only to
`robot_control_canopen_deny_transmit`. The project lifecycle object adds `recv`
and `setsockopt` but no transmit syscall dependency. `cppcheck` remains
unavailable and no result is claimed.

## RK3588 cross evidence

Both builds used the locked Docker image with network disabled and a clean
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

Debug and Release each exited 0, completed 42/42 steps, and passed interpreter,
needed-library, symbol-version, Phase 3 symbol, and no-RPATH audits. Metadata
records revision `05a948660596ecab3ec2f4bf487976b0cb60865b`, `dirty: false`,
659 source files, and snapshot SHA-256
`a6a8a091c7193f6d072843d4c3dc6a423a2a27dcec60da672aabbf544e61d8ff`.

| Preset | Artifact / metadata | SHA-256 |
|---|---|---|
| Debug | `out/artifacts/rk3588-debug/robot-control-platform-probe` | `60467297cc86e281af7a48774c95bb4c8d7f4819e8afa63def1f557ae23b65e6` |
| Debug | `out/artifacts/rk3588-debug/build-metadata.json` | `dbb4f3d171b324af6a25840995992d6e305906ebc4ed880be8211beb76dbef5b` |
| Debug | `librobot_control_canopen_upstream.a` | `bd08d440a9b4aa4abb91742edf0cde2f3ba2c247ebfd666948d166fb84be7a79` |
| Debug | `librobot_control_communication_canopen.a` | `9643e7c715b682214051df970ef3b1e45f0eb784922e67b997689c5fafb8813a` |
| Release | `out/artifacts/rk3588-release/robot-control-platform-probe` | `35ad27e6aad21e4c5dbbe384b597b7a1868e07739b4627872c94653e5f03389a` |
| Release | `out/artifacts/rk3588-release/build-metadata.json` | `fcad0906f20286d91fd88c034483a6af6d2abbd49e078e53d016ddb731b1e491` |
| Release | `librobot_control_canopen_upstream.a` | `35754c2d9c2c294055e6d7d735b669602db9699120732cf1c0a3ad06196e443c` |
| Release | `librobot_control_communication_canopen.a` | `2d86c4ffc7e6e59da60a43fca3d33588c14e8810d00f7e27d7918671e43ba97a` |

## Semantic review

The scoped C/C++ review found and corrected two issues before closure:

1. Reader-only timeout derivation could change current flags without changing
   snapshot version. Timeout transitions now run in the sole owner and increment
   version once.
2. With `CO_DRIVER_ERROR_REPORTING=0`, the pinned driver did not request kernel
   CAN error frames. The project lifecycle now applies `CAN_RAW_ERR_FILTER` to
   the same socket without enabling upstream link-reset policy.

No blocking finding remains.

```yaml
review_result:
  schema_version: 1
  source_revision: 05a948660596ecab3ec2f4bf487976b0cb60865b
  scope:
    - communication/canopen/observation.hpp
    - communication/canopen/observation.cpp
    - communication/canopen/lifecycle.hpp
    - communication/canopen/lifecycle.cpp
    - communication/canopen/stack_config.hpp
    - communication/canopen/stack_config.cpp
    - tests/unit/canopen_stack_tests.cpp
  findings: []
  decision: PASS
```

## Deferred runtime evidence

No CAN socket runtime, `vcan`, physical CAN, RK3588 target, deployment, link
configuration, drive write, or motion operation was performed. P5.5 must prove
the actual SocketCAN `MSG_PEEK`/single-consume ordering, error-frame delivery,
boot/heartbeat/EMCY/TPDO sequences, exact live timeouts, interface loss/reopen,
signal exits, and independent zero-transmit bus observation.
