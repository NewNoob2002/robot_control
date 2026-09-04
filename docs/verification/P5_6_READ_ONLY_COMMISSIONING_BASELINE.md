# P5.6 Debug-only Read-only Commissioning Baseline

Status: **COMPLETE**

Implementation revision: `3b0b86eea7a9b79f578e1f80861ae1902001cbde`

Verification date: 2026-08-26

## Result

P5.6 adds one explicitly enabled, single-config Debug commissioning build.
Default Debug, Release, RK3588, application, and test artifacts remain on the
unconditional transmit-deny path. The commissioning variant permits only node
1 NMT Stopped or Pre-operational and one-at-a-time expedited SDO uploads from
the reviewed read-only whitelist in
`docs/plans/P5_6_READ_ONLY_COMMISSIONING_PLAN.md`.

No physical CAN interface, RK3588 commissioning executable, drive parameter,
RPDO, SDO download, NMT Operational, reset, broadcast, or motion command was
used in this slice.

## Implemented boundary

- `ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING` defaults to `OFF` and rejects
  Release or multi-config generators.
- Normal upstream objects resolve `send` only to
  `robot_control_canopen_deny_transmit`. Separate commissioning objects enable
  NMT master and resolve `send` only to the commissioning gate.
- The thread-local gate holds one exact expected frame. It retains authorization
  across `EINTR`, `EAGAIN`, and `ENOBUFS`, then consumes it after a successful
  or non-retryable submission. Unexpected identifiers, DLC, payload, length,
  flags, commands, nodes, or objects clear authorization and fail with
  `EACCES`.
- `CommissioningSession` requires fixed remote node 1, a nonzero current boot
  generation, and a fresh heartbeat before every authorization.
- SDO results retain the accepted raw frame, outcome, request/attempt
  generation, index/subindex, data length/data, and abort code. Unsolicited,
  wrong, late, duplicate, canceled, and mixed-generation frames remain
  non-current diagnostics and increment rejection or replay evidence.
- Timeout closes the upstream client, cancels the token, and runs one
  SDO-timeout receive-only quarantine before the optional single retry.

## Managed-vcan evidence

Active testing used `/tmp/robot_control_p56_preflight.yaml` and the existing
namespace-local managed-vcan runner. The peer checked both NMT frames, exact
SDO requests, expedited data, abort parsing, a timed-out first attempt, exactly
one retry, a late quarantine response, and a post-completion duplicate. The
independent monitor captured 13 reviewed frames and no prohibited frame:

```text
INFO: managed namespace vcan0 is up
INFO: P5.6 managed-vcan frames=13 prohibited=0
```

The P5.5 managed regression also passed on the final source, again reporting 14
peer frames and zero observer TX.

## Host, sanitizer, and LLVM evidence

The commissioning Debug build completed with GCC 13.3 and warnings as errors.
Its 26 non-managed tests passed 26/26. This includes all invalid CLI cases, the
normal CANopen observation/storage contract, and direct gate tests for every
allowed whitelist entry, prohibited motion/configuration entries, both allowed
NMT commands, unexpected-frame fail-closed behavior, and retained authorization
across local socket backpressure.

Default-off Debug and Release builds each passed the unchanged 18/18
non-SocketCAN regression suite. Configuration with commissioning enabled in
Release failed as required.

ASan/UBSan passed the CANopen observation and commissioning-gate Host tests and
the complete commissioning managed-vcan scenario with
`ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer remains disabled because ptrace is
unavailable in this environment; no LSAN result is claimed.

LLVM 22.1.8 `clang-format` passed for the commissioning and observation sources,
headers, CLI, lifecycle header, and new tests. Scoped `clang-tidy` passed for
observation, gate, session, CLI, and the affected Host/managed tests with
clang-analyzer, bugprone, performance, and portability checks.
`git diff --check` passed. No `cppcheck` result is claimed.

## Artifact isolation

The default Debug build contains no commissioning executable. Normal upstream
and communication archives contain no commissioning authorization symbol; the
normal driver resolves only `robot_control_canopen_deny_transmit`. The
commissioning upstream archive resolves its driver only to
`robot_control_canopen_commissioning_transmit`.

## Verification commands

```bash
rtk cmake -S . -B build/p56-host-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING=ON
rtk cmake --build build/p56-host-debug -j2
rtk ctest --test-dir build/p56-host-debug --output-on-failure \
  -E '(socketcan_vcan_managed|canopen_vcan_managed|canopen_commissioning_vcan_managed)'
rtk bash scripts/test/test_socketcan_vcan.sh \
  build/p56-host-debug/tests/unit/robot_control_canopen_commissioning_vcan_tests

rtk ctest --test-dir /tmp/robot_control_p56_debug --output-on-failure \
  -E '^(socketcan_socket_lifecycle|socketcan_vcan_managed|canopen_vcan_managed)$'
rtk ctest --test-dir /tmp/robot_control_p56_release --output-on-failure \
  -E '^(socketcan_socket_lifecycle|socketcan_vcan_managed|canopen_vcan_managed)$'

rtk env ASAN_OPTIONS=detect_leaks=0 ctest \
  --test-dir /tmp/robot_control_p56_sanitize --output-on-failure \
  -R '^(canopen_stack_build|canopen_commissioning_gate)$'
rtk env ASAN_OPTIONS=detect_leaks=0 bash scripts/test/test_socketcan_vcan.sh \
  /tmp/robot_control_p56_sanitize/tests/unit/robot_control_canopen_commissioning_vcan_tests

rtk /opt/llvm-22.1.8/bin/clang-tidy \
  communication/canopen/observation.cpp communication/canopen/commissioning.cpp \
  communication/canopen/commissioning_gate.c tools/canopen_commission/main.cpp \
  tests/unit/canopen_stack_tests.cpp tests/unit/canopen_commissioning_gate_tests.cpp \
  tests/unit/canopen_commissioning_vcan_tests.cpp -p build/p56-host-debug --quiet \
  --checks='-*,clang-analyzer-*,bugprone-*,-bugprone-unchecked-optional-access,performance-*,portability-*,-portability-avoid-pragma-once' \
  --warnings-as-errors='clang-analyzer-*,bugprone-*'

rtk env ROBOT_CONTROL_SYSROOT=$PWD/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=$PWD/sysroots/locks/rk3588-ubuntu2204-a685ab13.json \
  ROBOT_CONTROL_PRESET=rk3588-debug ./scripts/build/build_rk3588.sh
rtk env ROBOT_CONTROL_SYSROOT=$PWD/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=$PWD/sysroots/locks/rk3588-ubuntu2204-a685ab13.json \
  ROBOT_CONTROL_PRESET=rk3588-release ./scripts/build/build_rk3588.sh
```

| Host Debug commissioning artifact | SHA-256 |
|---|---|
| `robot-control-canopen-commission` | `d98a09e26159515f328ac6c835195fa400a8a56a32641ed9dc05b734d04e4912` |
| `librobot_control_canopen_upstream_commissioning.a` | `cf1b70ed04439f00f8da254d317899ee0e4d68bbeba09c9ed7960311640116b7` |
| `librobot_control_communication_canopen_commissioning.a` | `1e1b727dbb1e30ea0c52f2cff311d6c2bd784f8bbdef6bcc93f7d84b880be0e6` |

## RK3588 cross evidence

The locked, network-disabled RK3588 Debug and Release builds used GCC 11.4 and
the `a685ab13` sysroot. Each completed 42/42 steps and passed interpreter,
needed-library, symbol-version, Phase 3 symbol, and no-RPATH audits. Both
metadata records report revision `3b0b86eea7a9b79f578e1f80861ae1902001cbde`,
`dirty: false`, 672 source files, and snapshot SHA-256
`b2ea65f66dd4a3cfcdaffc55c9aedc33cca5287bc9a0de9aa1d80c70b0f01da7`.

| Preset | Artifact | SHA-256 |
|---|---|---|
| Debug | `robot-control-platform-probe` | `60467297cc86e281af7a48774c95bb4c8d7f4819e8afa63def1f557ae23b65e6` |
| Debug | `build-metadata.json` | `31f34b1a2f8af03ef4d5be2292f04b20d7995598eb7449f379a99b99b651c534` |
| Debug | `librobot_control_canopen_upstream.a` | `bd08d440a9b4aa4abb91742edf0cde2f3ba2c247ebfd666948d166fb84be7a79` |
| Debug | `librobot_control_communication_canopen.a` | `7c2f3e6848ccac0d9278c63a8703f3ea972e413ad792da6b1905f13ec790e75c` |
| Release | `robot-control-platform-probe` | `35ad27e6aad21e4c5dbbe384b597b7a1868e07739b4627872c94653e5f03389a` |
| Release | `build-metadata.json` | `e77f59b49d50e9c1a293fb605beb12c6facba8748719925ca900b990bef71cc2` |
| Release | `librobot_control_canopen_upstream.a` | `35754c2d9c2c294055e6d7d735b669602db9699120732cf1c0a3ad06196e443c` |
| Release | `librobot_control_communication_canopen.a` | `49ec268740280c582ca3816d9e0df2c188605bda53a1ff1af8fa9e3db04565e4` |

## Semantic review

The final C/C++ review covered the complete gate/session/observation call path,
upstream retry behavior, syscall arguments, owner-thread lifetime, generation
invalidation, timeout cleanup, C/C++ layout compatibility, CLI trust boundary,
and default artifact linkage. Three analyzer findings were corrected before
closure: swappable C whitelist parameters became one object identifier,
authorization clearing no longer uses `memset`, and duplicate clearing branches
were collapsed. Manual review additionally added fixed-node validation and
token cancellation on a successful-upstream/result-correlation mismatch. No
blocking correctness, safety, concurrency, portability, security, or test
adequacy finding remains.

## Evidence contracts

```yaml
bus_validation:
  schema_version: 1
  safety_preflight: /tmp/robot_control_p56_preflight.yaml
  interface: can
  adapter_or_resource: namespace-local-vcan0
  settings:
    can: {nominal_bitrate: not-applicable-vcan, listen_only: false}
  requirements: [P5.6-exact-NMT, P5.6-read-only-SDO, P5.6-zero-prohibited-TX]
  stimulus: managed node-1 boot/heartbeat and reviewed 0x581 responses
  measurements_or_responses: exact NMT/upload/abort/timeout/retry/late/duplicate assertions
  raw_capture: test-owned independent SocketCAN monitor
  report: docs/verification/P5_6_READ_ONLY_COMMISSIONING_BASELINE.md
  cleanup: {required: true, completed: true, final_state: namespace-removed}
  passed: true

test_result:
  schema_version: 1
  source_revision: 3b0b86eea7a9b79f578e1f80861ae1902001cbde
  safety_preflight: /tmp/robot_control_p56_preflight.yaml
  level: integration
  target: host-managed-vcan
  attempts: 1
  passed: true
  counts: {total: 1, passed: 1, failed: 0, skipped: 0}
  requirements: [P5.6]

review_result:
  schema_version: 1
  source_revision: 3b0b86eea7a9b79f578e1f80861ae1902001cbde
  scope: [commissioning-gate, commissioning-session, SDO-observation, CLI, tests, CMake-isolation]
  toolchain_context: GCC-13.3-host, LLVM-22.1.8-analysis, GCC-11.4-aarch64-cross, C11/C++20
  findings: []
  verification: [host-26-of-26, default-debug-18-of-18, default-release-18-of-18, managed-vcan, ASan-UBSan, LLVM, artifact-audit, rk3588-debug-release]

release_evidence:
  schema_version: 1
  version: P5.6
  source_revision: 3b0b86eea7a9b79f578e1f80861ae1902001cbde
  safety_preflights: [/tmp/robot_control_p56_preflight.yaml, /tmp/robot_control_p55_regression_preflight.yaml]
  build_outputs: [host-commissioning-debug, host-default-debug, host-default-release, rk3588-debug, rk3588-release]
  deployments: []
  debug_sessions: []
  runtime_monitors: [managed-vcan-independent-monitor]
  bus_validations: [P5.6-managed-vcan, P5.5-managed-vcan-regression]
  tests: [host-26-of-26, default-debug-18-of-18, default-release-18-of-18, ASan-UBSan]
  reviews: [LLVM-22.1.8, semantic-C-C++-review, artifact-isolation-audit]
```

## Residual boundary

P5.6 proves the commissioning path only on managed vcan. Physical-drive SDO/NMT
exercise requires a separate explicit authorization, target identity, unloaded
motion-safe setup, allowed-frame preflight, independent capture, and cleanup.
P5.7 owns passive target and Phase 5 closure evidence; it does not inherit
permission to run this commissioning executable.
