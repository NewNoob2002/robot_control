# Phase 3 Linux Platform Baseline

Status date: 2026-07-31.

## Scope

Phase 3 implements policy-free Linux mechanisms and an observability base:

- move-only RAII ownership for file descriptors;
- operation, resource, and `errno` context for syscall failures;
- `poll(2)` readable, timeout, hang-up, and error observations;
- absolute `CLOCK_MONOTONIC` sleeping and drift-free periodic deadlines;
- synchronous SIGINT/SIGTERM consumption through `signalfd(2)`;
- raw nonblocking POSIX UART access with explicit standard baud rates;
- caller-owned, serialized structured logging with steady-clock throttling.

No SocketCAN, CANopen, CiA402 transition policy, command arbitration, safety
policy, ROS2, device-tree configuration, or physical device access is included.

## Ownership and lifecycle contracts

- `UniqueFd` is the only descriptor owner used by the new adapters. It is
  move-only and closes exactly once.
- `TerminationEvent::create()` must run in the lifecycle-owner thread before
  worker threads are created so they inherit the blocked signal mask.
- `SerialPort` owns its descriptor and returns zero bytes for a read timeout;
  disconnect and syscall failures return context-rich errors.
- `Logger` owns no stream. The caller must keep the stream alive and must not
  write it concurrently without external synchronization.
- Monotonic timestamps are used for deadlines and throttling. Wall-clock time
  appears only in human-readable log metadata.

## Integration coverage

`linux_platform_integration` validates:

- descriptor move and close behavior;
- pipe timeout/readiness and invalid-descriptor errors;
- absolute sleep and periodic deadline advancement;
- SIGTERM delivery, consumption, and empty nonblocking reads;
- PTY raw configuration, baud selection, fragmented byte reads, timeout,
  disconnect, and missing-device failures;
- structured severity/module fields and deterministic suppression counts.

PTY and pipe tests deliberately avoid real UART and CAN hardware.

## Verification commands

```bash
./scripts/build/build_host.sh

cmake -S . -B out/build/host-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build out/build/host-sanitize
ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir out/build/host-sanitize --output-on-failure

clang-tidy -p out/build/host-test \
  platform/linux/io/poll_wait.cpp \
  platform/linux/time/monotonic_timer.cpp \
  platform/linux/process/termination_event.cpp \
  platform/linux/uart/serial_port.cpp \
  service/logging/logger.cpp \
  -checks="-*,clang-analyzer-*,bugprone-*,performance-*,portability-*" \
  -warnings-as-errors="*"

ROBOT_CONTROL_SYSROOT="$PWD/sysroots/rk3588-ubuntu2204" \
  ./scripts/build/build_rk3588.sh
```

## Acceptance evidence

- warnings-as-errors host build passes all four CTest tests;
- ASan/UBSan build passes all four CTest tests;
- selected clang-analyzer, bugprone, performance, and portability checks pass;
- RK3588 release cross build compiles every Phase 3 library and validates the
  probe interpreter, symbol versions, dependencies, and absence of RPATH;
- include/content audits find no control policy in `platform/linux` and no
  Linux mechanism dependency in `domain`;
- `clang-format --dry-run --Werror` and `git diff --check` pass.

LeakSanitizer is disabled only in the current ptrace-based Codex execution
surface; it must run in normal CI.

## Deferred validation

- SBUS requires a verified 100000 baud, 8E2 configuration. Phase 3 exposes only
  standard termios baud constants; the Linux-specific custom baud mechanism is
  selected and tested with the actual UART in the SBUS phase.
- Signal-mask inheritance must be verified again when the production thread
  topology exists.
- Real UART disconnect behavior and latency remain target/HIL tests.
- SocketCAN mechanics begin in Phase 4.
