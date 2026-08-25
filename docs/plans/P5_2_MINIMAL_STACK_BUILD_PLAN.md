# P5.2 Minimal CANopen Stack Build Implementation Plan

Status: **APPROVED FOR EXECUTION**

Plan date: 2026-08-25

## Goal

Build and host-test the smallest project-owned CANopen integration slice that
allocates one deterministic stack context and fixed object dictionary for one
local controller, remote drive node 1, one heartbeat consumer, one EMCY
consumer, one SDO client, and four receive-PDO slots. P5.2 compiles the pinned
CANopenLinux/CANopenNode sources but never opens a CAN interface or submits a
CAN frame.

## Fixed implementation decisions

- Use the pinned CANopenLinux SocketCAN driver. Do not create another CANopen
  transport over `platform/linux/can::CanSocket`.
- Compile with `CO_MULTIPLE_OD`, `CO_SINGLE_THREAD`, and `CO_DRIVER_CUSTOM`.
  `CO_MULTIPLE_OD` supplies explicit runtime counts to `CO_new()`;
  `CO_SINGLE_THREAD` matches the single-owner Phase 5 model.
- Keep upstream C sources and the generated OD on a target without
  `robot_control_project_warnings`. Project-owned C++ remains warning-clean and
  warning-as-error. Upstream include directories are CMake `SYSTEM` includes.
- Copy the pinned upstream generated `example/OD.c` and `example/OD.h` into
  `communication/canopen/od/` without editing their generated bodies. Runtime
  configuration narrows the active counts and values; unused generated entries
  do not enable modules.
- Record the source XPD and checksums in
  `communication/canopen/od/PROVENANCE.md`; do not add a generator, download, or
  configure-time generation step.
- Disable normal-build NMT master, EMCY producer, SDO server, TPDO, SYNC, TIME,
  LSS, storage, gateway, LEDs, GFC, SRDO, trace, and Linux driver link mutation.
  `CO_DRIVER_ERROR_REPORTING` is zero in P5.2 so the upstream helper cannot
  alter interface state.
- Enable only NMT/heartbeat processing, heartbeat-consumer callbacks/queries,
  EMCY consumption, one SDO client, FIFO support required by that client, and
  RPDO reception. No commissioning transmit call is added.
- The generated OD uses mutable globals. Hide them behind one process-wide
  `StackStorage` ownership claim, justify that claim in Doxygen, reject a second
  simultaneous owner with `EBUSY`, and test release/reacquisition. This is the
  minimum safe bridge until a future requirement genuinely needs multiple
  independent CANopen contexts.
- P5.2 must not call `CO_CANinit()`, `CO_CANopenInit()`,
  `CO_CANopenInitPDO()`, `CO_CANsetNormalMode()`, `CO_epoll_*()`, socket APIs,
  `if_nametoindex()`, or any send/process function. Those lifecycle operations
  belong to P5.3 and later authorized slices.

`communication/canopen/CO_driver_custom.h` uses this exact normal-build feature
set unless the pinned compiler proves a named constant unavailable:

```c
#define CO_CONFIG_NMT \
  (CO_CONFIG_NMT_CALLBACK_CHANGE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_HB_CONS \
  (CO_CONFIG_HB_CONS_ENABLE | CO_CONFIG_HB_CONS_CALLBACK_MULTI | \
   CO_CONFIG_HB_CONS_QUERY_FUNCT | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_NODE_GUARDING 0
#define CO_CONFIG_EM \
  (CO_CONFIG_EM_CONSUMER | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_SDO_SRV 0
#define CO_CONFIG_SDO_CLI \
  (CO_CONFIG_SDO_CLI_ENABLE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_SDO_CLI_BUFFER_SIZE 32U
#define CO_CONFIG_TIME 0
#define CO_CONFIG_SYNC 0
#define CO_CONFIG_PDO \
  (CO_CONFIG_RPDO_ENABLE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_LEDS 0
#define CO_CONFIG_LSS 0
#define CO_CONFIG_GFC 0
#define CO_CONFIG_SRDO 0
#define CO_CONFIG_GTW 0
#define CO_CONFIG_CRC16 0
#define CO_CONFIG_FIFO CO_CONFIG_FIFO_ENABLE
#define CO_CONFIG_STORAGE 0
#define CO_CONFIG_TRACE 0
#define CO_CONFIG_DEBUG 0
#define CO_DRIVER_ERROR_REPORTING 0
#define CO_DRIVER_MULTI_INTERFACE 0
```

## Exact upstream inputs

Dependency revisions remain:

- CANopenLinux: `f1348d4072cdabea4c3435a13c721ac29ab4cc91`
- CANopenNode: `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`

The initial upstream source target contains only:

```text
components/CANopenLinux/CO_driver.c
components/CANopenLinux/CANopenNode/CANopen.c
components/CANopenLinux/CANopenNode/301/CO_ODinterface.c
components/CANopenLinux/CANopenNode/301/CO_NMT_Heartbeat.c
components/CANopenLinux/CANopenNode/301/CO_HBconsumer.c
components/CANopenLinux/CANopenNode/301/CO_Emergency.c
components/CANopenLinux/CANopenNode/301/CO_SDOserver.c
components/CANopenLinux/CANopenNode/301/CO_SDOclient.c
components/CANopenLinux/CANopenNode/301/CO_PDO.c
components/CANopenLinux/CANopenNode/301/CO_fifo.c
communication/canopen/od/OD.c
```

`CO_SDOserver.c` is linked because `CO_CANopenInit()` retains the runtime-count
branch under `CO_MULTIPLE_OD`; the active SDO-server count remains zero. Do not
add `CO_epoll_interface.c`, `CO_error.c`, storage, LSS, SYNC, TIME, TPDO-only,
gateway, LED, safety, or trace sources in this slice. If the pinned source
proves an additional link dependency, document the exact unresolved symbol and
add only its owning source; do not enable its feature macro.

Pinned OD provenance inputs:

| Input | SHA-256 |
|---|---|
| `components/CANopenLinux/CANopenNode/example/OD.c` | `ef52b2a2189fd450cda65c4e0bd663478e1dfcaf778d6f2de296705c2e766bc1` |
| `components/CANopenLinux/CANopenNode/example/OD.h` | `737d5c0ca4685d8f2acea14c996836bee2845551aaf7db7cf005689b3c7299fd` |
| `components/CANopenLinux/CANopenNode/example/DS301_profile.xpd` | `3c941038809ba14d0e7ecce74678f3607b21373ec56d4301e3cecec87dc25d33` |

## Public project-owned interface

Create `communication/canopen/stack_config.hpp` with:

```cpp
struct StackConfig {
  std::string interface_name{};
  std::uint8_t controller_node_id{0};
  std::uint8_t remote_node_id{1};
  std::uint16_t bit_rate_kbit_s{500};
  std::chrono::milliseconds heartbeat_timeout{0};
  std::chrono::milliseconds sdo_timeout{0};
};

[[nodiscard]] platform::linux::Status
validate_stack_config(const StackConfig &config) noexcept;
```

Validation is pure and performs no interface lookup:

- controller and remote node IDs are each `1..127` and must differ;
- interface name is nonempty, shorter than `IFNAMSIZ`, not `.` or `..`, and
  contains no slash, whitespace, or control character;
- bit rate is one of the documented ZLAC8015D values
  `{100, 125, 250, 500, 1000}` kbit/s;
- heartbeat and SDO timeouts are `1..65535 ms`, preventing narrowing into the
  upstream 16-bit interfaces.

The empty interface, controller ID zero, and zero timeouts are intentional
invalid defaults that force startup injection. Unit tests use an explicit valid
sample (`can0`, controller 127, remote 1, 500 kbit/s, heartbeat 1000 ms, SDO
500 ms); those test values are not deployment defaults.

Create `communication/canopen/stack_storage.hpp` with a noncopyable, nonmovable
`StackStorage` owned through `std::unique_ptr`:

```cpp
class StackStorage final {
public:
  using CreateResult =
      platform::linux::Result<std::unique_ptr<StackStorage>>;

  [[nodiscard]] static CreateResult create(StackConfig config) noexcept;
  ~StackStorage();

  StackStorage(const StackStorage &) = delete;
  StackStorage &operator=(const StackStorage &) = delete;
  StackStorage(StackStorage &&) = delete;
  StackStorage &operator=(StackStorage &&) = delete;

  [[nodiscard]] CO_t *stack() noexcept;
  [[nodiscard]] OD_t *object_dictionary() noexcept;
  [[nodiscard]] const CO_config_t &upstream_config() const noexcept;
  [[nodiscard]] std::uint32_t heap_memory_used() const noexcept;
  [[nodiscard]] const StackConfig &config() const noexcept;
};
```

All new project functions require English Doxygen covering purpose, parameters,
return value, thread safety, and ownership. `create()` performs the steps below
in order and unwinds the ownership claim on every failure:

1. Validate `StackConfig`.
2. Claim the single generated-OD owner atomically; return operation
   `claim_canopen_stack`, context containing both node IDs, and `EBUSY` if held.
3. Reset the copied OD mutable groups to their generated defaults before applying
   configuration, so sequential contexts cannot inherit modified state.
4. Set heartbeat producer time to zero. Set consumer sub-count to one, clear all
   entries, and encode `(remote_node_id << 16) | heartbeat_timeout_ms` in the
   first `0x1016` entry.
5. Configure `0x1280` for remote SDO server COB-IDs `0x600 + node` and
   `0x580 + node`, with the remote node ID.
6. Configure RPDO1..4 to receive the remote node's TPDO1..4 identifiers
   `0x180/0x280/0x380/0x480 + remote_node_id`; leave all mappings at zero and
   do not initialize or transmit.
7. Run `OD_INIT_CONFIG`, then explicitly set active counts to NMT 1, HB consumer
   1 with one monitored node, EM 1, SDO server 0, SDO client 1, TIME 0, SYNC 0,
   RPDO 4, TPDO 0, and all optional counts 0.
8. Call only `CO_new()` and retain its heap-memory count. Map null allocation to
   operation `CO_new`, the same identity context, and `ENOMEM`.
9. Destruction calls `CO_delete()` before releasing the generated-OD claim.

## Task 1: Add failing host contract tests

**Files**

- Create: `tests/unit/canopen_stack_tests.cpp`
- Create: `tests/unit/canopen_test_log_stub.c`
- Modify: `tests/unit/CMakeLists.txt`

Add one `canopen_stack_build` CTest executable following the existing `CHECK`
pattern. The test-only `log_printf()` stub satisfies the pinned Linux driver
link contract and must not enter production targets. Before implementation, the
new target must fail because the project-owned CANopen headers/target do not
exist.

Required assertions:

- compile-time feature macros enable only the fixed normal-build subset;
- the explicit valid sample and all validation boundaries above;
- syntactically valid missing interface `missing0` succeeds through storage
  creation, proving no interface lookup/socket activation;
- upstream counts are exactly `1/1/1/0/1/0/0/4/0` for
  NMT/HB/EM/SDO-server/SDO-client/TIME/SYNC/RPDO/TPDO;
- heartbeat, SDO, and four RPDO COB-ID values match the requested remote node;
- `CO_new()` returns non-null storage and a nonzero stable heap size;
- a simultaneous second owner fails with `EBUSY`; after destruction, a new owner
  succeeds and observes regenerated defaults plus its own configuration;
- no CAN descriptor, interface index, or normal-mode state is created.

Run the target once and retain the expected missing-header/target failure before
adding implementation.

## Task 2: Add minimal CMake targets and project-owned storage

**Files**

- Modify: `CMakeLists.txt`
- Create: `communication/canopen/CMakeLists.txt`
- Create: `communication/canopen/CO_driver_custom.h`
- Create: `communication/canopen/stack_config.hpp`
- Create: `communication/canopen/stack_config.cpp`
- Create: `communication/canopen/stack_storage.hpp`
- Create: `communication/canopen/stack_storage.cpp`
- Create: `communication/canopen/od/OD.c`
- Create: `communication/canopen/od/OD.h`
- Create: `communication/canopen/od/PROVENANCE.md`

Add `add_subdirectory(communication/canopen)` before tools. Define:

- `robot_control_canopen_upstream`: static C library containing exactly the
  source list above, project custom configuration, and system include paths;
- `robot_control_communication_canopen`: project-owned C++ library containing
  configuration/storage and linking the upstream target plus
  `robot_control_project_options` and `robot_control_project_warnings`.

Do not add `CO_main_basic.c`, an executable, a socket activation function, or
any fetch/generator command. Make the Task 1 test green, then run the complete
non-SocketCAN host Debug suite.

## Task 3: Add Host Release build coverage and sustainable CI

**Files**

- Modify: `CMakePresets.json`
- Modify: `.github/workflows/ci.yml`

Add `host-release` configure, build, and test presets inheriting `base`, with
`CMAKE_BUILD_TYPE=Release` and `BUILD_TESTING=ON`. Keep the existing P5.1 Debug
gate and Phase 4 SocketCAN gate separate. Add a CI step after the P5.1 gate:

```bash
cmake --preset host-release -S "${GITHUB_WORKSPACE}"
cmake --build --preset host-release
ctest --preset host-release --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'
```

Do not run the two excluded tests as part of P5.2 evidence. The existing
separate Phase 4 CI step remains unchanged.

## Task 4: Verify, commit clean implementation, and cross-build

Run from the repository root, in this order:

```bash
rtk ./scripts/test/test_canopen_dependencies.sh
rtk cmake --preset host-test -S /home/gtc/Desktop/workspace/Linux_PROJ/robot_control
rtk cmake --build --preset host-test
rtk ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'
rtk cmake --preset host-release -S /home/gtc/Desktop/workspace/Linux_PROJ/robot_control
rtk cmake --build --preset host-release
rtk ctest --preset host-release --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'
rtk git diff --check 3d02421 HEAD -- . ':(exclude)communication/canopen/od/OD.h'
rtk cmp communication/canopen/od/OD.c components/CANopenLinux/CANopenNode/example/OD.c
rtk cmp communication/canopen/od/OD.h components/CANopenLinux/CANopenNode/example/OD.h
rtk git submodule status --recursive
```

The generated upstream `OD.h` contains six pre-existing trailing-whitespace
lines. Preserve it byte-for-byte: exclude only that file from the whitespace
check, and use the exact `cmp` commands plus recorded SHA-256 values as its
content gate.

Inspect `compile_commands.json` or verbose build output to prove excluded source
files are absent and project/upstream warning policies are separated. Then stage
explicit implementation paths and commit:

```bash
rtk git add CMakeLists.txt CMakePresets.json .github/workflows/ci.yml communication/canopen tests/unit/CMakeLists.txt tests/unit/canopen_stack_tests.cpp tests/unit/canopen_test_log_stub.c
rtk git commit -m "feat: add minimal CANopen stack build"
```

With the worktree clean, run the existing network-disabled cross builds:

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

Both commands must exit 0 and pass the existing ELF audits. They do not authorize
target, CAN, `vcan`, deployment, or motion activity.

## Task 5: Record P5.2 evidence and handoff

**Files**

- Create: `docs/verification/P5_2_STACK_BUILD_BASELINE.md`
- Modify: `docs/plans/PHASE5_CANOPEN_INTEGRATION.md`
- Modify: `AGENTS.md`

Record source/dependency revisions, exact host Debug/Release and cross commands,
exit codes, host test counts, source-list audit, OD provenance checksums, cross
artifact/metadata paths, and the explicit no-CAN execution statement. Include
host `test_result` records only; P5.2 has no HIL behavior to claim. Mark P5.2
complete only after all acceptance checks have evidence, then commit explicit
documentation paths:

```bash
rtk git add AGENTS.md docs/plans/PHASE5_CANOPEN_INTEGRATION.md docs/verification/P5_2_STACK_BUILD_BASELINE.md
rtk git commit -m "docs: record P5.2 stack build baseline"
```

## Acceptance mapping

| P5.2 acceptance | Required evidence |
|---|---|
| Host Debug and Release compile selected sources | both presets build and non-SocketCAN CTest pass |
| RK3588 Debug and Release compile/audit | both clean-source cross commands exit 0 with metadata |
| Deterministic single-owner OD storage | ownership/reacquisition and exact-count/value unit tests |
| Invalid startup configuration fails before activation | pure validation boundary tests plus `missing0` no-lookup test |
| Upstream warnings isolated | CMake target review and compile-command audit |
| Unused modules disabled | compile-time macro assertions and exact source-list audit |
| No runtime generator/network fetch | checked-in OD/provenance and offline configure/build logs |
| No CAN operation in P5.2 | command audit excludes both SocketCAN tests and all runtime tools |

## Stop conditions

- Stop rather than editing either submodule.
- Stop if the pinned API requires enabling a producer, gateway, storage, LSS,
  SYNC, TIME, TPDO, or link-configuration path merely to compile.
- Stop before any CAN/`vcan`, target, deployment, driver, device-tree, or motion
  operation.
- Do not claim P5.2 complete if Release cross is blocked by a dirty source;
  commit intentional source first rather than bypassing the gate.
