# Phase 2 Pure Domain Baseline

Status date: 2026-07-31.

## Scope

Phase 2 implements protocol- and platform-independent control policy:

- monotonic time aliases and freshness checks;
- typed immutable command samples;
- source/session/sequence/authorization generations;
- manual-priority command arbitration;
- 150 ms both-sources-zero handover qualification;
- external authorization replay rejection;
- safety startup inhibit, communication loss, drive fault/reset, zero-only
  enable sequence, and motion rearm;
- complete standard CiA402 state-mask decoding for neutral low/high halves;
- velocity-mode eligibility;
- newer-observation and 500 ms transition timeout tracking.

No Linux, CANopenNode, ROS2, file descriptor, device, logging, allocation
framework, or hardware access is present in `domain/`.

## Behavior-vector traceability

| Vector | Automated assertion |
|---|---|
| ARB-001 | healthy non-zero SBUS immediately owns and external is gated |
| ARB-002 | lost/invalid SBUS cannot reveal an old external command |
| ARB-003 | 149 ms zero dwell remains inhibited |
| ARB-004 | old authorization generation remains inhibited after dwell |
| ARB-005 | fresh generation after 150 ms dwell may select external |
| ARB-006 | incoherent and sequence-replayed inputs fail closed |
| SAFE-001 | unqualified startup is `startup_inhibit` with zero output |
| SAFE-002 | non-zero selected command rejects fault reset |
| SAFE-003 | fresh zero reset request with qualified feedback is eligible |
| SAFE-004 | stale heartbeat enters CANopen-unavailable and revokes motion |
| SAFE-005 | recovery rejects former authorization and accepts a newer one |
| CIA-001 | switch-on-disabled maps to zero-target shutdown action |
| CIA-002 | transition needs a newer observation and times out at 500 ms |
| CIA-003 | operation-enabled state with mode mismatch is not motion eligible |
| CIA-004 | unknown mask remains unknown and preserves raw status |
| CIA-005 | low/high halves decode independently without physical axis labels |

Additional assertions cover every standard CiA402 state, exact freshness
boundaries, future timestamps, invalid negative durations, and invalid session
generation.

SBUS-001 through SBUS-004 remain specifications for Phase 9 because Phase 2
does not introduce the SBUS byte-stream protocol parser. Moving that parser
into Phase 2 would violate the approved staged scope.

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

ROBOT_CONTROL_SYSROOT="$PWD/sysroots/rk3588-ubuntu2204" \
  ./scripts/build/build_rk3588.sh
```

## Acceptance evidence

- normal host build uses warnings-as-errors and passes all CTest tests;
- ASan/UBSan build and tests pass;
- RK3588 release cross build compiles all domain libraries;
- domain include audit finds no Linux, ROS2, CANopenNode, termios, pthread,
  SocketCAN, filesystem, or unistd dependencies;
- `git diff --check` passes.

Host tests prove pure policy behavior only. They do not prove CAN transport,
target scheduling, SBUS UART behavior, drive conformance, or motor safety.
LeakSanitizer is disabled in the current Codex execution surface because tests
run under ptrace and LSan terminates before test execution; AddressSanitizer and
UndefinedBehaviorSanitizer remain enabled. Leak checking must be enabled again
in normal CI.
