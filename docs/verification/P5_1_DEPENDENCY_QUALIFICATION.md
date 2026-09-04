# P5.1 Dependency Qualification Evidence

Evidence date: 2026-08-25 UTC. Result: **PASS (remediated)**.

The closure batch qualified source revision
`37fa3351baac620c9558806eea00d92fb9022ce4`. It ran dependency, recursive
snapshot, non-SocketCAN host, static, and RK3588 Debug/Release cross checks
without target access or CAN/`vcan` runtime operations. Target-runtime HIL is
not applicable because P5.1 deliberately links no CANopen source.

## Source and dependency identity

- CANopenLinux: `f1348d4072cdabea4c3435a13c721ac29ab4cc91`
- Nested CANopenNode: `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`
- Root and nested index modes: `160000`
- Canonical URLs: `https://github.com/CANopenNode/CANopenLinux.git` and
  `https://github.com/CANopenNode/CANopenNode.git`
- Licenses: Apache-2.0
- Local patches: none; both dependency working trees clean
- Source snapshot: 634 files, SHA-256
  `bdef21181bc22b0705e50afd5c94c05e9dbcbedd311d443d91ff8de0bcb9f0c6`

The production verifier fixes both expected revisions, validates exact
submodule paths, URLs, gitlinks, checkouts, and cleanliness, and fails closed
when `git status` cannot establish cleanliness. Regression coverage includes
wrong revisions, dirty content, top-level and nested uninitialized submodules,
wrong paths, and separate top-level/nested status failures.

## Remediation qualification results

| Result | Command | Evidence |
|---|---|---|
| PASS | `./scripts/build/verify_cross_image.sh` | locked image ID `sha256:f2198e31e27c084bc2deff761e124fa9d7ce580a8d986c7885fd62bb1701e7dd`; architecture/toolchain/package manifest verified |
| PASS | `./scripts/test/test_canopen_dependencies.sh` | exact pair and all negative paths pass |
| PASS | `./scripts/test/test_phase1_scripts.sh` | recursive snapshot fixture contains CANopenLinux and nested CANopenNode files |
| PASS | `./scripts/test/test_sysroot_manifest.sh` | sysroot content manifest regressions pass |
| PASS | `cmake --preset host-test -S /home/gtc/Desktop/workspace/Linux_PROJ/robot_control` | configure exit 0 |
| PASS | `cmake --build --preset host-test` | build exit 0 |
| PASS | `ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'` | 17/17 passed; both SocketCAN runtime tests excluded |
| PASS | `shellcheck` over all changed P5.1 shell scripts | ShellCheck 0.9.0, exit 0 |
| PASS | RK3588 Debug cross command from the plan | clean source, Docker `--network none`, compile and ELF audit pass |
| PASS | RK3588 Release cross command from the plan | clean source, reviewed Git-tracked sysroot lock, Docker `--network none`, compile and ELF audit pass |
| PASS | `git diff --check` and recursive submodule status | no whitespace error; exact clean submodules |

ShellCheck 0.9.0 is installed at `/home/gtc/.local/bin/shellcheck`, which is on
the user PATH. The package was downloaded from the configured Ubuntu package
source and installed without root privileges. Its first run detected SC2155 in
the dependency regression; the declaration and assignment were separated and
the regression plus ShellCheck then passed.

## Cross artifacts

Both artifacts use source revision
`37fa3351baac620c9558806eea00d92fb9022ce4`, the same clean 634-file source
snapshot, GNU aarch64 11.4.0, and sysroot content SHA-256
`a685ab13c6e2087dde9ec29e0f6c18a49cf0af477692f2e3a2b0b8b6e5c39911`.

| Preset | Exit | Artifact | Metadata | SHA-256 |
|---|---:|---|---|---|
| `rk3588-debug` | 0 | `out/artifacts/rk3588-debug/robot-control-platform-probe` | `out/artifacts/rk3588-debug/build-metadata.json` | `7d20a00a263c1f45eba603393acf7f53733965bed65667ac0a086e5960db7932` |
| `rk3588-release` | 0 | `out/artifacts/rk3588-release/robot-control-platform-probe` | `out/artifacts/rk3588-release/build-metadata.json` | `a0bd2c42b46d7a9dd95ccbbaa2c8f39f9294f43500bc99e715b00865f37c89ab` |

Exact Debug command (exit 0):

```bash
rtk env \
  ROBOT_CONTROL_SYSROOT=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204.lock.json \
  ROBOT_CONTROL_PRESET=rk3588-debug \
  ./scripts/build/build_rk3588.sh
```

Exact Release command (exit 0):

```bash
rtk env \
  ROBOT_CONTROL_SYSROOT=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/locks/rk3588-ubuntu2204-a685ab13.json \
  ROBOT_CONTROL_PRESET=rk3588-release \
  ./scripts/build/build_rk3588.sh
```

Both ELF audits validate the aarch64 interpreter, sysroot-resolved shared
libraries and symbol versions, required Phase 3 symbols, and absence of
RPATH/RUNPATH.

## Historical no-CAN deviation and remediation

The initial qualification attempt mistakenly ran the complete host CTest suite
outside the sandbox. Its isolated namespace runner created a temporary `vcan`
interface and transmitted virtual frames. It did not access a physical CAN
interface, RK3588 target, drive, persistent configuration, or motion path. This
was nevertheless contrary to the P5.1 no-CAN execution boundary and remains a
recorded process deviation.

The deviation was remediated by a separate closure batch on clean revision
`37fa3351baac620c9558806eea00d92fb9022ce4`. That batch explicitly excluded
`socketcan_socket_lifecycle` and `socketcan_vcan_managed`, passed all 17
remaining host tests, ShellCheck, dependency/script regressions, and both
network-disabled cross builds. No SocketCAN runtime, target, deployment, drive,
or motion command was executed in the closure batch. This batch supersedes the
failed initial qualification for the completion decision without erasing its
history.

```yaml
test_result:
  schema_version: 1
  source_revision: "37fa3351baac620c9558806eea00d92fb9022ce4"
  artifact_checksums: {}
  level: integration
  target: "x86_64 host"
  command: "ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'"
  timing: {started_at_utc: "2026-08-25T03:58:06Z", duration_ms: 255, timeout_ms: 0}
  attempts: 1
  passed: true
  counts: {total: 17, passed: 17, failed: 0, skipped: 0}
  classified_failures: []
  log: "command table in this document"
  requirements: ["P5.1-HOST-001"]
```

HIL remains not applicable rather than passed. It is recorded independently so
that the prohibited-operation audit and explicit SocketCAN exclusions cannot be
misread as executed HIL evidence.

```yaml
test_result:
  schema_version: 1
  source_revision: "37fa3351baac620c9558806eea00d92fb9022ce4"
  artifact_checksums: {}
  level: hil
  target: "not-applicable-by-phase-scope"
  command: "not run: P5.1 links no CANopen source"
  timing: {started_at_utc: null, duration_ms: 0, timeout_ms: 0}
  attempts: 0
  passed: false
  counts: {total: 0, passed: 0, failed: 0, skipped: 0}
  classified_failures: []
  log: "no HIL log; no target, physical CAN, drive, or vcan operation executed"
  requirements: ["P5.1-HIL-001"]
```

## P5.2 handoff

P5.2 may rely on `components/CANopenLinux`, its nested
`components/CANopenLinux/CANopenNode`, the fail-fast verifier, recursive CI
checkout, submodule-complete offline snapshots, and clean Debug/Release cross
qualification. It may not rely on target-runtime or CANopen protocol behavior
from P5.1.
