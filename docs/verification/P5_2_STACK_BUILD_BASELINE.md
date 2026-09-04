# P5.2 Minimal CANopen Stack Build Baseline

Evidence date: 2026-08-25 UTC. Result: **PASS**.

Implementation revision `bd88a72453f7b071b56d0ab628ce9ab5cd95af59`
builds and host-tests one inactive CANopen allocation. The qualification used
only local host CMake/CTest, dependency checks, compile-command inspection, and
network-disabled Docker cross builds. It did not open, inspect, configure, or
transmit on any CAN interface.

## Red/green contract evidence

The test was added before the implementation. Its first narrow build failed as
expected at `canopen_stack_tests.cpp:1` with:

```text
fatal error: communication/canopen/CO_driver_custom.h: No such file or directory
ninja: build stopped: subcommand failed.
```

After the minimum implementation, the narrow target built in 18 steps and
`ctest --test-dir out/build/host-test --tests-regex '^canopen_stack_build$'
--output-on-failure` passed 1/1. The test covers exact macros and active counts,
all configuration boundaries, `missing0` allocation without interface lookup,
heartbeat/SDO/RPDO OD values, nonzero stable `CO_new()` heap size, simultaneous
`EBUSY`, release/reacquisition and regenerated defaults, plus null interfaces,
zero interface count, and false normal-mode state.

Host Release initially exposed three existing ignored `write()` return warnings
in `tests/integration/linux_platform_tests.cpp` under `-Werror`. The minimum
fix stores those return values in `[[maybe_unused]]` locals; test behavior is
unchanged. The subsequent fresh Debug and Release qualifications both passed.

## Source and provenance identity

- CANopenLinux: `f1348d4072cdabea4c3435a13c721ac29ab4cc91`, clean
- Nested CANopenNode: `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`, clean
- Local patches to either submodule: none
- Generated OD source: CANopenNode example `DS301_profile.xpd`, CANopenEditor
  `v4.1-1-ga49a51a`, checked in without a generator or fetch step

| Input | SHA-256 |
|---|---|
| `communication/canopen/od/OD.c` | `ef52b2a2189fd450cda65c4e0bd663478e1dfcaf778d6f2de296705c2e766bc1` |
| `communication/canopen/od/OD.h` | `737d5c0ca4685d8f2acea14c996836bee2845551aaf7db7cf005689b3c7299fd` |
| source `DS301_profile.xpd` | `3c941038809ba14d0e7ecce74678f3607b21373ec56d4301e3cecec87dc25d33` |

`cmp` proves both copied generated files are byte-identical to the pinned
example inputs.

## Host qualification

All commands ran from the repository root and exited 0:

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

The scoped diff check excludes only generated `OD.h`, whose pinned upstream
input contains six trailing-whitespace lines. Both `cmp` commands exited 0, and
the checksums above prove the copied generated files remain byte-identical.

| Preset | Tests | Passed | Failed | Skipped | Duration |
|---|---:|---:|---:|---:|---:|
| `host-test` (Debug) | 18 | 18 | 0 | 0 | 0.25 s |
| `host-release` | 18 | 18 | 0 | 0 | 0.25 s |

Both commands excluded exactly `socketcan_socket_lifecycle` and
`socketcan_vcan_managed`; neither test ran.

## Source-list and warning audit

`out/build/host-test/compile_commands.json` and the Release equivalent contain
exactly these 11 upstream/generated C inputs:

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

The audit found zero compile commands for epoll interface, error helper,
storage, LSS, SYNC, TIME, GFC, SRDO, gateway, LED, or trace sources. Upstream C
commands use `CO_DRIVER_CUSTOM`, `CO_MULTIPLE_OD`, and `CO_SINGLE_THREAD`, with
upstream headers as system includes and without project warning flags. Project
`stack_config.cpp` and `stack_storage.cpp` use C++20 plus `-Wall -Wextra
-Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wformat=2 -Wundef -Werror`.
Their source contains zero calls to `CO_CANinit`, `CO_CANopenInit`,
`CO_CANopenInitPDO`, `CO_CANsetNormalMode`, `CO_epoll_*`, socket APIs,
`if_nametoindex`, `CO_CANsend`, or process functions.

The exact fixed feature macro set is compile-time asserted. Active runtime
counts are `1/1/1/0/1/0/0/4/0` for NMT/HB/EM/SDO-server/SDO-client/TIME/SYNC/
RPDO/TPDO; all optional counts are zero. `CO_SDOserver.c` is linked only for
the pinned `CO_MULTIPLE_OD` runtime-count branch and its active count is zero.

## RK3588 cross build outputs

Both clean-source commands ran in the locked Docker image with network disabled
and exited 0. Each built 38/38 steps and validated interpreter, needed
libraries, symbol versions, required Phase 3 symbols, and absence of RPATH.

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

Both metadata files record clean revision
`bd88a72453f7b071b56d0ab628ce9ab5cd95af59`, 649 source files, snapshot
SHA-256 `1fb7b5cc71c6c35b9ac2de9cbd06fd4471ede05728f490be436876433cc6e614`,
GNU aarch64 11.4.0, and sysroot content SHA-256
`a685ab13c6e2087dde9ec29e0f6c18a49cf0af477692f2e3a2b0b8b6e5c39911`.

| Preset | Exit | Artifact / metadata | SHA-256 |
|---|---:|---|---|
| Debug | 0 | `out/artifacts/rk3588-debug/robot-control-platform-probe` | `7d20a00a263c1f45eba603393acf7f53733965bed65667ac0a086e5960db7932` |
| Debug | 0 | `out/artifacts/rk3588-debug/build-metadata.json` | `90e36bda679d85030b13613b5c93b61e99c8db84d12122429f0aa3dce7e32c3e` |
| Release | 0 | `out/artifacts/rk3588-release/robot-control-platform-probe` | `a0bd2c42b46d7a9dd95ccbbaa2c8f39f9294f43500bc99e715b00865f37c89ab` |
| Release | 0 | `out/artifacts/rk3588-release/build-metadata.json` | `b854ea2656cae832f79d5261fe1258f6f07fa370f0999ca5c1fb5cf0bf3265a4` |

The built CANopen static libraries remain build-tree artifacts rather than
published runtime executables. Debug checksums are upstream
`1824af628598d79662e4929b7672eb037bdc1be61ba7a639bd530656fbdaa209`
and project-owned `f3a131eeca1b5fe4943b9d2ae1994b08bc78594349f656c8acba4a3620430c8d`;
Release checksums are upstream
`a87666f595f368573d1c76ba4cda6c97265b2e5e8ab9cdfc04bba1f810031bed`
and project-owned `b7e311e92f977162dd48aa94abcbec998394145c007ce99c9d1fa1087028a463`.

## Host test-result records

```yaml
test_result:
  schema_version: 1
  source_revision: "bd88a72453f7b071b56d0ab628ce9ab5cd95af59"
  artifact_checksums: {}
  level: host-unit
  target: "x86_64 host Debug"
  command: "ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'"
  timing: {started_at_utc: "2026-08-25", duration_ms: 250, timeout_ms: 0}
  attempts: 1
  passed: true
  counts: {total: 18, passed: 18, failed: 0, skipped: 0}
  classified_failures: []
  log: "host qualification table in this document"
  requirements: ["P5.2-HOST-DEBUG"]
```

```yaml
test_result:
  schema_version: 1
  source_revision: "bd88a72453f7b071b56d0ab628ce9ab5cd95af59"
  artifact_checksums: {}
  level: host-unit
  target: "x86_64 host Release"
  command: "ctest --preset host-release --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'"
  timing: {started_at_utc: "2026-08-25", duration_ms: 250, timeout_ms: 0}
  attempts: 1
  passed: true
  counts: {total: 18, passed: 18, failed: 0, skipped: 0}
  classified_failures: []
  log: "host qualification table in this document"
  requirements: ["P5.2-HOST-RELEASE"]
```

P5.2 has no HIL result to claim. No CAN or `vcan` test, SocketCAN runtime,
physical CAN, RK3588 access, driver/network/device-tree configuration,
deployment, persistent drive operation, or motion command was executed. Cross
compilation is host-side only. Runtime lifecycle and passive/active CAN evidence
remain explicitly deferred to their later authorized slices.

## Independent review

An independent `gpt-5.6-sol` medium review of `3d02421..77779633` found no
Critical defect and approved P5.2 closure. It approved P5.3 planning only: the
P5.3 contract must deny the pinned stack's initial NMT boot-up transmission and
must clear heap-backed OD extensions across init failure, teardown, and reopen
before implementation begins. These are P5.3 lifecycle requirements, not P5.2
allocation defects. The review performed no CAN, `vcan`, hardware, deployment,
or repository operation.
