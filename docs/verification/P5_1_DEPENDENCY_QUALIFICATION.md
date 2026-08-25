# P5.1 Dependency Qualification Evidence

Evidence date: 2026-08-25 UTC. Result: **PARTIAL**.

P5.1 installed the immutable dependency pair and verified recursive offline
source snapshots. The completion gate is not met because Release cross was not
run while the protected pre-existing `docker/cross/image.lock` modification
keeps the source snapshot dirty, ShellCheck is unavailable locally, and one
isolated managed-`vcan` test was mistakenly executed despite the P5.1 no-CAN
boundary.

## Source and dependency identity

- Evidence source revision: `00511d5f102646bb908bd6ca68467a310da60b62`
- CANopenLinux: `f1348d4072cdabea4c3435a13c721ac29ab4cc91`
- Nested CANopenNode: `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`
- Root and nested index modes: `160000`
- Canonical URLs: `https://github.com/CANopenNode/CANopenLinux.git` and
  `https://github.com/CANopenNode/CANopenNode.git`
- Licenses: Apache-2.0
- Local patches: none; both dependency working trees clean

`./scripts/test/test_canopen_dependencies.sh` exited 0. Its clean check printed
both revisions with `dirty=false` and the nested relationship `result=pass`;
its temporary wrong-revision verifier copy returned 3 and its temporary nested
dirty marker returned 4. The production verifier has no expected-revision
override. Before dependency installation the same regression returned 2 at
the missing CANopenLinux path, providing the required RED result.

## Review corrections

Post-qualification review found that the first verifier could accept an
environment-provided expected CANopenLinux revision, could let
`rev-parse --is-inside-work-tree` resolve to a parent repository, and did not
explicitly classify `git status` failure. TDD review assertions first failed on
the production expected-revision override. The corrected verifier now:

- fixes both expected revisions in production code;
- requires each `--show-toplevel` canonical path to equal its exact submodule
  path;
- checks the top-level `.gitmodules` path as well as URL;
- treats either dependency `git status` failure as exit 4 before printing any
  clean result.

The regression uses a temporary verifier copy for the 40-zero SHA, an
uninitialized local-submodule fixture for exit 2, a failing status wrapper for
exit 4 with no `dirty=false` output, and a wrong-path wrapper for exit 3. No
real dependency checkout is changed by these negative tests. The qualification
status remains **PARTIAL** for the previously recorded reasons.

## Verification results

| Result | Command | Evidence |
|---|---|---|
| PASS | `bash -n scripts/build/verify_canopen_dependencies.sh` and `bash -n scripts/test/test_canopen_dependencies.sh` | exit 0 |
| PASS | `./scripts/test/test_canopen_dependencies.sh` | exit 0; identity and negative paths passed |
| PASS | `./scripts/test/test_phase1_scripts.sh` | exit 0; recursive fixture contains `CO_driver.c` and nested `CANopen.c` |
| PASS | `./scripts/build/create_source_snapshot.sh out/p5_1-source-snapshot out/p5_1-source-attestation.json` | exit 0; 634 files; SHA-256 `ca08b74f5130fcb7d80254359d9f7fd3a76062294d4215279bf5888e2cc12d11`; both dependency levels inspected |
| PASS | `./scripts/test/test_sysroot_manifest.sh` | exit 0 |
| PASS | `cmake --preset host-test -S /home/gtc/Desktop/workspace/Linux_PROJ/robot_control` | exit 0 |
| PASS | `cmake --build --preset host-test` | exit 0 |
| PASS | `ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'` | exit 0; 17/17 passed; SocketCAN runtime tests excluded by scope |
| PASS | `git diff --check` | exit 0 |
| SKIP | `shellcheck scripts/build/verify_canopen_dependencies.sh scripts/test/test_canopen_dependencies.sh scripts/build/create_source_snapshot.sh scripts/build/build_host.sh scripts/build/build_rk3588.sh scripts/test/test_phase1_scripts.sh` | tool unavailable; `which shellcheck` exit 1 |
| PASS | Debug cross command from the plan | exit 0 after sandbox Docker-socket retry; Docker used `--network none`; aarch64 compile and ELF audit passed |
| SKIP | Release cross command from the plan | not run: protected `docker/cross/image.lock` modification makes the required clean source attestation unavailable |

The Debug artifact is
`out/artifacts/rk3588-debug/robot-control-platform-probe`, SHA-256
`7d20a00a263c1f45eba603393acf7f53733965bed65667ac0a086e5960db7932`.
It used GNU aarch64 11.4.0, sysroot artifact `rk3588-ubuntu2204`, sysroot content
SHA-256 `a685ab13c6e2087dde9ec29e0f6c18a49cf0af477692f2e3a2b0b8b6e5c39911`,
external lock SHA-256
`7e8e4e0f0e75606ebfa3e0104f5e67f8e559d07f72deb8503769a5072af3e68c`,
and container image ID
`sha256:f2198e31e27c084bc2deff761e124fa9d7ce580a8d986c7885fd62bb1701e7dd`.
The artifact attestation correctly records the source as dirty.

## No-CAN boundary deviation

The first sandboxed `./scripts/build/build_host.sh` returned 8 because the
environment rejected its PF_CAN socket operation; 17/19 tests ran, one failed,
and managed `vcan` skipped. A sandbox-external retry was then mistakenly run as
the full command. It exited 0 with 19/19 tests passed, including
`socketcan_vcan_managed`. That test used an isolated namespace and did not
access a physical interface, target, drive, or persistent configuration, but
it did execute temporary `vcan` operations and frames contrary to this task's
no-CAN boundary. No further SocketCAN runtime test was run. Therefore
P5.1-HIL-001 and the no-CAN completion condition are not claimed as PASS.

No target address, target command, deployment, physical CAN operation, drive
connection, motion command, or persistent parameter write was used.

```yaml
test_result:
  schema_version: 1
  evidence_timestamp_utc: "2026-08-25T03:03:16Z"
  source_revision: "00511d5f102646bb908bd6ca68467a310da60b62"
  level: hil
  target: RK3588/ZLAC8015D
  command: "not executed: P5.1 links no CANopen source and has no target behavior"
  attempts: 0
  passed: false
  tests_total: 0
  tests_passed: 0
  tests_failed: 0
  tests_skipped: 0
  classified_failure: not-applicable-by-phase-scope
  log: none
  requirement: P5.1-HIL-001
```

Here `passed: false` means no target HIL pass is claimed; it is not a product
failure. Separately, the isolated managed-`vcan` deviation above prevents this
execution from satisfying the P5.1 no-CAN completion condition.

## P5.2 handoff

P5.2 may rely on `components/CANopenLinux`, its nested
`components/CANopenLinux/CANopenNode`, the fail-fast verifier, recursive CI
checkout, and submodule-complete offline source snapshots. It may not rely on
any target-runtime, CANopen behavior, or HIL result from P5.1.
