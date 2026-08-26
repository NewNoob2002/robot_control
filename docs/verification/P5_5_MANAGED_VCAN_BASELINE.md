# P5.5 Managed vcan Receive Baseline

Date: 2026-08-26

Implementation revision: `2b291c1d4d14982ec28839dcf65a6de1165dc307`

## Outcome

P5.5 passes with no blocking implementation or verification finding. One new
managed-vcan test drives the production `Lifecycle` owner through real
SocketCAN receive, timeout, error, link, reopen, and signal paths. The existing
capability-aware namespace runner remains the only link-configuration owner.

The first configured run exposed that the pinned stack did not install TPDO
socket filters when the fixed observation-only RPDO mappings had zero mapped
objects. The lifecycle now installs one exact project-owned receive set after
upstream normal-mode setup: NMT service plus the configured remote node's EMCY,
TPDO1..4, SDO response, and heartbeat identifiers. This does not add a second
socket or transport. Unmatched TPDO frames are peeked, consumed once by the
pinned upstream socket path, and then classified by the project observation
store.

The first run also confirmed that namespace-local `vcan` link-down does not
reliably produce the epoll error/hangup event assumed by the endpoint-only
path. The owner now checks administrative link state every 10 ms. A down link
causes one bounded reopen attempt, invalidates the transport generation, and
returns contextual `ENETDOWN` when the interface remains down.

No SDO request was sent. The normal SDO result slot remains empty, as required;
read-only SDO request/response work remains P5.6. No physical CAN, target,
deployment, drive write, NMT command, RPDO, or motion operation was performed.

## Managed-vcan evidence

The Debug, Release, and ASan/UBSan variants each ran the same namespace-local
test and exited 0 in about 0.52 seconds:

```bash
rtk ctest --test-dir /tmp/robot_control_p55_debug \
  -R '^canopen_vcan_managed$' --output-on-failure -V

rtk ctest --test-dir /tmp/robot_control_p55_release \
  -R '^canopen_vcan_managed$' --output-on-failure -V

rtk env ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir /tmp/robot_control_p55_sanitize \
  -R '^canopen_vcan_managed$' --output-on-failure -V
```

Each configured run proved:

- startup and every later owner-processing interval produced zero observer TX;
- an independent socket saw exactly the 14 injected peer/error frames and no
  additional frame;
- boot `0x701#00`, operational heartbeat `0x701#05`, EMCY `0x081`, and
  TPDO1..4 `0x181/0x281/0x381/0x481` retained exact identifier, DLC, all eight
  raw data bytes, owner monotonic timestamp, transport generation, and boot
  generation;
- each injected data frame advanced the snapshot version exactly once, proving
  the `MSG_PEEK` tap did not consume or duplicate the upstream receive;
- TPDO observations were current at the configured 150 ms boundary and stale
  at boundary plus 1 ns; heartbeat/NMT were current at the configured 300 ms
  boundary and stale at boundary plus 1 ns; owner timer processing also
  invalidated them after the live deadlines;
- the injected `CAN_ERR_BUSOFF | CAN_ERR_CRTL` frame
  `0x20000044#0004123456789abc` reached the production socket through
  `CAN_RAW_ERR_FILTER`, preserved its raw bytes, advanced transport generation,
  and invalidated remote state;
- heartbeat after either CAN error or endpoint reopen stayed non-current until
  a fresh boot-up frame established a new boot generation;
- link-down was detected within the bounded 100 ms assertion window and
  reported `ioctl(SIOCGIFFLAGS)`, `state=down`, errno 100 (`ENETDOWN`); link-up
  plus explicit reopen restored reception; and
- open-endpoint SIGINT and SIGTERM each returned their exact lifecycle exit
  reason without transmitting a frame.

Representative Debug output preserved the following raw observations:

```text
service=boot      raw_can_id=0x00000701 dlc=1 payload=0000000000000000 transport=1 boot=1
service=heartbeat raw_can_id=0x00000701 dlc=1 payload=0500000000000000 transport=1 boot=1
service=emcy      raw_can_id=0x00000081 dlc=8 payload=34125678efcdab90 transport=1 boot=1
service=tpdo1     raw_can_id=0x00000181 dlc=8 payload=1122334455667788 transport=1 boot=1
service=tpdo2     raw_can_id=0x00000281 dlc=0 payload=0000000000000000 transport=1 boot=1
service=tpdo3     raw_can_id=0x00000381 dlc=0 payload=0000000000000000 transport=1 boot=1
service=tpdo4     raw_can_id=0x00000481 dlc=0 payload=0000000000000000 transport=1 boot=1
service=can_error raw_can_id=0x20000044 dlc=8 payload=0004123456789abc transport=1 boot=1
summary: peer_frames=14 observer_tx_frames=0
```

Actual monotonic nanosecond values differed per run and were checked as ordered,
nonzero runtime observations rather than fixed golden values.

## Host build, regression, sanitizer, and static checks

Host Debug and Release configurations built with GCC 13.3 and warnings as
errors. Each passed the 18/18 non-SocketCAN suite:

```bash
rtk ctest --test-dir /tmp/robot_control_p55_debug --output-on-failure \
  -E '^(socketcan_socket_lifecycle|socketcan_vcan_managed|canopen_vcan_managed)$'
rtk ctest --test-dir /tmp/robot_control_p55_release --output-on-failure \
  -E '^(socketcan_socket_lifecycle|socketcan_vcan_managed|canopen_vcan_managed)$'
```

The excluded Phase 4 SocketCAN tests require a configured interface and are not
silently counted as host-only passes. P5.5's own managed test ran separately in
both configurations and did not skip or retry.

GCC AddressSanitizer and UndefinedBehaviorSanitizer passed the CANopen unit and
managed-vcan tests with only LeakSanitizer disabled for the known ptrace
environment limitation. LLVM 22.1.8 `clang-format` and scoped `clang-tidy`
passed for `lifecycle.cpp`, `lifecycle.hpp`, and `canopen_vcan_tests.cpp`.
`git diff --check` passed. No `cppcheck` result is claimed.

The upstream driver object still resolves transmit attempts only through
`robot_control_canopen_deny_transmit`. The lifecycle object has undefined
references to `ioctl`, `recv`, and `setsockopt`, but no transmit syscall.

## RK3588 cross evidence

Clean source revision `2b291c1d4d14982ec28839dcf65a6de1165dc307` passed
the locked, network-disabled RK3588 Debug and Release builds. Each completed
42/42 build steps plus interpreter, needed-library, symbol-version, Phase 3
symbol, and no-RPATH audits. Both metadata records report `dirty: false`, 661
source files, and snapshot SHA-256
`db82375f77913909e1ecd23eee7e5f9a85d9aae3e0e6b39fa32cc388e82d0846`.

| Preset | Artifact | SHA-256 |
|---|---|---|
| Debug | `robot-control-platform-probe` | `60467297cc86e281af7a48774c95bb4c8d7f4819e8afa63def1f557ae23b65e6` |
| Debug | `build-metadata.json` | `87d753ffc3348df26994e9d2cd63350810a5621a7d25658f832b80148d2c25f4` |
| Debug | `librobot_control_canopen_upstream.a` | `bd08d440a9b4aa4abb91742edf0cde2f3ba2c247ebfd666948d166fb84be7a79` |
| Debug | `librobot_control_communication_canopen.a` | `c782aeea684b189008a24c1785ab471c3c7eff8f8627ab8cf9e394625122a69a` |
| Release | `robot-control-platform-probe` | `35ad27e6aad21e4c5dbbe384b597b7a1868e07739b4627872c94653e5f03389a` |
| Release | `build-metadata.json` | `ae263165f8d9abd0a3cbbb461e2c404000c05168e3fe01f6d3a141bed9698b0c` |
| Release | `librobot_control_canopen_upstream.a` | `35754c2d9c2c294055e6d7d735b669602db9699120732cf1c0a3ad06196e443c` |
| Release | `librobot_control_communication_canopen.a` | `db4a6c0fb39ac65c248934bf2f8d7b6ee45228c7d73aa5cc8b83e585c8b5e327` |

## Evidence contracts

```yaml
safety_preflight:
  schema_version: 1
  timestamp_utc: "2026-08-26T07:28:10Z"
  operation: bus-write
  authorization: "P5.5 managed-vcan test requested by the user"
  target:
    board: "ephemeral Linux network namespace"
    hardware_revision: not-applicable
    probe_or_adapter_serial: not-applicable
    transport: can
  artifact_or_stimulus: "Debug, Release, and ASan/UBSan canopen_vcan_tests binaries listed in the verification commands"
  limits: "vcan0 only; IDs 0x701, 0x081, 0x181, 0x281, 0x381, 0x481 and one CAN_ERR_BUSOFF|CAN_ERR_CRTL frame; no physical CAN, NMT command, SDO, RPDO, or motion; 10 s CTest timeout"
  recovery_plan: "namespace exit removes vcan0 and all sockets"
  approved: true

bus_validation:
  schema_version: 1
  created_at_utc: "2026-08-26T07:28:10Z"
  safety_preflight: "embedded above"
  interface: can
  adapter_or_resource: "namespace-local vcan0"
  settings:
    can: {nominal_bitrate: "virtual", listen_only: false}
  requirements: [P5.5-RAW, P5.5-TIMEOUT, P5.5-ERROR, P5.5-REOPEN, P5.5-SIGNAL, P5.5-ZERO-TX]
  stimulus: "14 bounded peer/error frames described above"
  measurements_or_responses: "exact observation and lifecycle assertions in robot_control_canopen_vcan_tests"
  raw_capture: "verbose CTest output reviewed during qualification; representative records retained above"
  report: docs/verification/P5_5_MANAGED_VCAN_BASELINE.md
  cleanup: {required: true, completed: true, final_state: "network namespace exited"}
  passed: true

review_result:
  schema_version: 1
  source_revision: 2b291c1d4d14982ec28839dcf65a6de1165dc307
  scope:
    - communication/canopen/lifecycle.cpp
    - communication/canopen/lifecycle.hpp
    - tests/unit/canopen_vcan_tests.cpp
    - tests/unit/CMakeLists.txt
  findings: []
  decision: PASS
```

## Deferred work

P5.6 still owns all read-only SDO request authorization, request/response
correlation, abort, retry, timeout, and replay evidence. P5.7 still owns the
ordinary-user RK3588 passive observer run, target-side capture/process cleanup,
and final Phase 5 closure record.
