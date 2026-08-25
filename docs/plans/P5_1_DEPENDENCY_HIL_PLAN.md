# P5.1 Immutable CANopen Dependency Pair Qualification Plan

Status: **COMPLETE** — remediation qualification closed 2026-08-25.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the mixed CANopenNode snapshot with the exact CANopenLinux/CANopenNode pair selected by ADR-0002 and prove that normal host and RK3588 builds consume a clean, offline-reproducible source tree without performing CAN, drive, or target operations.

**Architecture:** Track CANopenLinux as the only top-level CANopen submodule and retain its upstream CANopenNode gitlink as the compatibility authority. Add one project-owned verifier, make the existing deterministic source snapshot include recursive submodule files, and reuse the current host/cross build pipelines. P5.1 contains no CANopen CMake target and therefore has no meaningful target-runtime or bus HIL stimulus.

**Tech Stack:** Git submodules, Bash, CMake/Ninja, CTest, Docker with `--network none`, Ubuntu 22.04 aarch64 sysroot, GitHub Actions.

**Spec:** `docs/plans/PHASE5_CANOPEN_INTEGRATION.md` section P5.1, `docs/decisions/0002-canopen-stack-and-logging-dependencies.md`, and `third_party/README.md`.

## Global Constraints

- CANopenLinux revision is exactly `f1348d4072cdabea4c3435a13c721ac29ab4cc91`.
- CANopenNode revision is exactly `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`.
- The nested CANopenNode gitlink recorded by CANopenLinux is the compatibility authority.
- Remove the mixed tracked snapshot at `components/CANopenNode`; do not preserve a second copy.
- Do not modify any upstream file or create a local third-party patch in P5.1.
- Do not add CANopen sources to CMake in P5.1.
- Normal configure/build/test commands must perform no network fetch.
- Do not open or configure CAN interfaces, connect to a drive, send CAN frames, deploy to the RK3588, or change target state.
- The cross-image lock must match the verified local image before it is committed or used for Release qualification.
- Use `rtk` for shell commands issued interactively.
- Every test wait and build invocation must be bounded by the existing CI/build timeout or an explicit `timeout`.

## Qualification Requirements

| ID | Requirement | Evidence |
| --- | --- | --- |
| P5.1-DEP-001 | Root gitlink resolves to the selected CANopenLinux commit | verifier output and `git ls-files --stage` |
| P5.1-DEP-002 | CANopenLinux records and checks out the selected nested CANopenNode commit | verifier output and nested `git ls-tree` |
| P5.1-DEP-003 | Both third-party working trees are clean | verifier plus dirty-tree negative test |
| P5.1-DEP-004 | Wrong or uninitialized revisions fail before build | verifier negative tests |
| P5.1-OFF-001 | Checked-out dependencies are sufficient for normal builds without fetch | recursive checkout, source snapshot test, cross build with Docker `--network none` |
| P5.1-SNAP-001 | Deterministic source snapshots include tracked files from both submodule levels | Phase 1 fixture regression and snapshot inspection |
| P5.1-HOST-001 | Existing host build and non-SocketCAN-runtime CTest suite remain green before CANopen linkage | CMake configure/build plus CTest excluding `socketcan_socket_lifecycle` and `socketcan_vcan_managed` |
| P5.1-XBUILD-001 | RK3588 Debug and Release builds remain green against the reviewed sysroot | `scripts/build/build_rk3588.sh` for both presets |
| P5.1-STATIC-001 | Changed shell/YAML/Markdown files pass applicable static checks | ShellCheck, workflow inspection, `git diff --check` |
| P5.1-HIL-001 | P5.1 performs no target-runtime or CAN-bus stimulus | prohibited-operation audit and explicit `not-applicable` HIL result |

## Test-Level Decision

The behavior under test is dependency identity and build reproducibility. Host integration and cross-build checks are the lowest levels that can falsify those requirements. Running an executable on the RK3588 cannot prove the identity of sources that are deliberately not linked in P5.1, so target-runtime HIL is **not applicable**, not a pass and not an infrastructure skip.

No `safety_preflight` is required because this plan contains no flash, reset, target-storage write, CAN write, interface change, deployment, or instrument output. If execution expands to any such operation, stop and obtain new explicit authorization before creating the required `safety_preflight`.

## Planned File Map

| Path | Responsibility |
| --- | --- |
| `.gitmodules` | Top-level immutable CANopenLinux source URL and path |
| `components/CANopenLinux` | Gitlink pinned to the selected CANopenLinux commit; owns nested CANopenNode gitlink |
| `components/CANopenNode` | Deleted mixed baseline snapshot |
| `scripts/build/verify_canopen_dependencies.sh` | Fail-fast revision, relationship, initialization, URL, and cleanliness gate |
| `scripts/test/test_canopen_dependencies.sh` | Positive and negative regression checks for the verifier |
| `scripts/build/create_source_snapshot.sh` | Include recursive tracked submodule files in deterministic build snapshots |
| `scripts/test/test_phase1_scripts.sh` | Local nested-submodule fixture proving snapshot completeness |
| `scripts/build/build_host.sh` | Invoke dependency verification before host configuration |
| `scripts/build/build_rk3588.sh` | Invoke dependency verification before source snapshot/cross build |
| `.github/workflows/ci.yml` | Recursive submodule checkout and dependency regression execution |
| `third_party/README.md` | Final immutable provenance and no-local-patch status |
| `AGENTS.md` and `README.md` | Current P5.1 status and remaining Phase 5 scope |
| `docs/verification/P5_1_DEPENDENCY_QUALIFICATION.md` | Exact commands, revisions, results, unavailable checks, and no-HIL audit |

---

### Task 1: Establish the immutable dependency pair and fail-fast verifier

**Files:**

- Create: `.gitmodules`
- Create: `components/CANopenLinux` gitlink
- Delete: `components/CANopenNode/**`
- Create: `scripts/build/verify_canopen_dependencies.sh`
- Create: `scripts/test/test_canopen_dependencies.sh`

**Interfaces:**

- Consumes: root Git index, top-level `.gitmodules`, CANopenLinux upstream `.gitmodules`, and both submodule working trees.
- Produces: `scripts/build/verify_canopen_dependencies.sh` with no arguments for the production gate; exit 0 prints exact revisions and `dirty=false`; exit 2 reports missing/uninitialized paths, exit 3 reports identity/relationship/URL mismatch, and exit 4 reports dirty third-party content.

- [ ] **Step 1: Record the protected baseline**

Run:

```bash
rtk git status --short --branch
rtk git diff -- docker/cross/image.lock
rtk git rev-parse HEAD
rtk cmake --preset host-test -S /home/gtc/Desktop/workspace/Linux_PROJ/robot_control
rtk cmake --build --preset host-test
rtk ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'
```

Expected: host build and 17 non-SocketCAN-runtime tests pass; `docker/cross/image.lock` remains the only pre-existing user-owned modification and is not staged. This plan document is the only expected task-owned untracked file. If another path is dirty, classify it before continuing. The two SocketCAN runtime tests are excluded because P5.1 authorizes no CAN or `vcan` socket/interface operation.

- [ ] **Step 2: Write the dependency verifier and regression driver**

The verifier must use fixed constants and these checks in order:

```bash
expected_linux=f1348d4072cdabea4c3435a13c721ac29ab4cc91
expected_node=ef9ac3a2279e34855a20c787fc1bc48bc995ec22
linux_path=components/CANopenLinux
node_path=components/CANopenLinux/CANopenNode
```

1. Both paths exist; each `git rev-parse --show-toplevel` succeeds and its canonical path exactly equals the expected submodule path rather than a parent repository.
2. `.gitmodules` maps the exact path `components/CANopenLinux` to `https://github.com/CANopenNode/CANopenLinux.git`.
3. The root index entry for `components/CANopenLinux` has mode `160000` and the expected CANopenLinux object ID.
4. CANopenLinux `HEAD` equals `expected_linux`.
5. CANopenLinux `HEAD` records `CANopenNode` as mode `160000` and object ID `expected_node`.
6. Nested CANopenNode `HEAD` equals `expected_node`.
7. CANopenLinux's `.gitmodules` records the canonical CANopenNode upstream URL.
8. `git status --porcelain=v1 --untracked-files=all` succeeds and is empty in both dependency working trees; command failure is a dirty-state verification failure.

Successful output must be stable key/value records:

```text
dependency=CANopenLinux revision=f1348d4072cdabea4c3435a13c721ac29ab4cc91 dirty=false
dependency=CANopenNode revision=ef9ac3a2279e34855a20c787fc1bc48bc995ec22 dirty=false
relationship=nested-gitlink result=pass
```

The regression driver must:

- run the clean-tree verifier successfully;
- copy the verifier to a uniquely named temporary file under `scripts/build`, replace only the fixed CANopenLinux SHA in that copy with a 40-zero SHA, and require exit 3; the production verifier has no expected-revision override;
- create one uniquely named untracked marker under the CANopenNode working tree, require exit 4, and remove the marker through an EXIT trap;
- reject execution if that marker already exists;
- use a temporary local Git fixture to require exit 2 first with the top-level submodule uninitialized and again after initializing only CANopenLinux while leaving nested CANopenNode uninitialized; the latter log must identify CANopenNode;
- inject separate test-only CANopenLinux and CANopenNode `git status` failures; each requires exit 4, its matching error, and no `dirty=false` output;
- inject a wrong top-level submodule path and require exit 3;
- verify the top-level legacy `components/CANopenNode` path is absent.

- [ ] **Step 3: Run the regression before replacing the dependency**

Run:

```bash
rtk bash -n scripts/build/verify_canopen_dependencies.sh
rtk bash -n scripts/test/test_canopen_dependencies.sh
rtk ./scripts/test/test_canopen_dependencies.sh
```

Expected: the final command fails with exit 2 because `components/CANopenLinux` is not initialized yet. Preserve the output as the required red test.

- [ ] **Step 4: Replace the mixed snapshot with the selected upstream pair**

Run only after confirming no user changes exist below `components/CANopenNode`:

```bash
rtk git status --short -- components/CANopenNode
rtk git rm -r components/CANopenNode
rtk git submodule add https://github.com/CANopenNode/CANopenLinux.git components/CANopenLinux
rtk git -C components/CANopenLinux checkout --detach f1348d4072cdabea4c3435a13c721ac29ab4cc91
rtk git -C components/CANopenLinux submodule update --init --recursive
rtk git -C components/CANopenLinux/CANopenNode checkout --detach ef9ac3a2279e34855a20c787fc1bc48bc995ec22
```

Do not edit either third-party worktree. If the nested gitlink at the selected CANopenLinux commit is not `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`, stop and report an ADR/upstream contradiction instead of forcing the nested checkout.

- [ ] **Step 5: Stage only dependency identity and run the green/negative checks**

Run:

```bash
rtk git add .gitmodules components/CANopenLinux scripts/build/verify_canopen_dependencies.sh scripts/test/test_canopen_dependencies.sh
rtk ./scripts/test/test_canopen_dependencies.sh
rtk git diff --cached --submodule=log -- .gitmodules components scripts/build/verify_canopen_dependencies.sh scripts/test/test_canopen_dependencies.sh
```

Expected: clean check passes; wrong revision exits 3; temporary dirty marker exits 4 and is removed; nested relationship passes.

- [ ] **Step 6: Commit the dependency identity slice**

```bash
rtk git commit -m "build: pin CANopenLinux dependency pair" -- .gitmodules components scripts/build/verify_canopen_dependencies.sh scripts/test/test_canopen_dependencies.sh
```

---

### Task 2: Make deterministic source snapshots submodule-complete

**Files:**

- Modify: `scripts/build/create_source_snapshot.sh`
- Modify: `scripts/test/test_phase1_scripts.sh`

**Interfaces:**

- Consumes: initialized, clean recursive submodules already validated by Task 1.
- Produces: the existing source snapshot format, now containing all root tracked/untracked files plus tracked recursive submodule files; existing attestation fields remain unchanged.

- [ ] **Step 1: Add a failing nested-submodule fixture**

Extend `scripts/test/test_phase1_scripts.sh` with three temporary local repositories:

1. `node-repo` containing committed `CANopen.c`;
2. `linux-repo` containing committed `CO_driver.c` and `CANopenNode` as a local submodule;
3. `snapshot-repo` containing the copied snapshot script and `components/CANopenLinux` as a local submodule.

Use `git -c protocol.file.allow=always submodule add` for fixture-only local paths. After recursive initialization, run the copied snapshot script and assert both files exist:

```bash
test -f "${snapshot_output}/components/CANopenLinux/CO_driver.c"
test -f "${snapshot_output}/components/CANopenLinux/CANopenNode/CANopen.c"
```

- [ ] **Step 2: Run the fixture and confirm the current snapshot implementation fails**

```bash
rtk ./scripts/test/test_phase1_scripts.sh
```

Expected: FAIL because the current root-only `git ls-files` archive does not include submodule files.

- [ ] **Step 3: Implement the minimum recursive file selection**

Replace the single root file-list producer with the union of:

```bash
git -C "${repo_root}" ls-files --cached --recurse-submodules -z
git -C "${repo_root}" ls-files --others --exclude-standard -z
```

Sort with `LC_ALL=C sort -zu`, retain the existing existence check, deterministic tar metadata, `--no-recursion`, archive checksum, dirty flag, and attestation schema. Do not copy `.git` directories or untracked files from dependency submodules.

- [ ] **Step 4: Run snapshot and script regressions**

```bash
rtk bash -n scripts/build/create_source_snapshot.sh
rtk ./scripts/test/test_phase1_scripts.sh
rtk ./scripts/test/test_canopen_dependencies.sh
```

Expected: all pass and the generated fixture snapshot contains both submodule levels.

- [ ] **Step 5: Commit the recursive snapshot support**

```bash
rtk git add scripts/build/create_source_snapshot.sh scripts/test/test_phase1_scripts.sh
rtk git commit -m "build: include submodules in source snapshots" -- scripts/build/create_source_snapshot.sh scripts/test/test_phase1_scripts.sh
```

---

### Task 3: Enforce dependency qualification in normal builds and CI

**Files:**

- Modify: `scripts/build/build_host.sh`
- Modify: `scripts/build/build_rk3588.sh`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**

- Consumes: Task 1 verifier and Task 2 submodule-complete source snapshot.
- Produces: fail-fast local/CI build gates and recursive CI checkout.

- [ ] **Step 1: Write the failing build-gate assertions**

Add exact checks to `scripts/test/test_canopen_dependencies.sh` requiring both build entry points to contain:

```text
scripts/build/verify_canopen_dependencies.sh
```

Also require `.github/workflows/ci.yml` to contain `submodules: recursive` and a direct invocation of `./scripts/test/test_canopen_dependencies.sh`.

- [ ] **Step 2: Run the regression and confirm it fails**

```bash
rtk ./scripts/test/test_canopen_dependencies.sh
```

Expected: FAIL because the build and CI gates have not been added.

- [ ] **Step 3: Add the minimum gates**

- Call the verifier immediately after computing `repo_root` in `build_host.sh`.
- Call the verifier before sysroot validation and source snapshot creation in `build_rk3588.sh`.
- Set `submodules: recursive` on the pinned `actions/checkout` step.
- Add one CI step named `Verify CANopen dependency pair` before the host build.

Do not add CANopen CMake targets or new packages.

- [ ] **Step 4: Run host and static verification**

```bash
rtk ./scripts/test/test_canopen_dependencies.sh
rtk cmake --preset host-test -S /home/gtc/Desktop/workspace/Linux_PROJ/robot_control
rtk cmake --build --preset host-test
rtk ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'
rtk ./scripts/test/test_phase1_scripts.sh
rtk ./scripts/test/test_sysroot_manifest.sh
rtk shellcheck scripts/build/verify_canopen_dependencies.sh scripts/test/test_canopen_dependencies.sh scripts/build/create_source_snapshot.sh scripts/build/build_host.sh scripts/build/build_rk3588.sh scripts/test/test_phase1_scripts.sh
rtk git diff --check
```

Expected: all commands pass; exactly 17 non-SocketCAN-runtime host tests run. The two SocketCAN runtime tests are excluded by the P5.1 no-CAN boundary, not reported as pass or infrastructure skip.

- [ ] **Step 5: Commit build and CI enforcement**

```bash
rtk git add scripts/build/build_host.sh scripts/build/build_rk3588.sh .github/workflows/ci.yml scripts/test/test_canopen_dependencies.sh
rtk git commit -m "ci: verify pinned CANopen dependencies" -- scripts/build/build_host.sh scripts/build/build_rk3588.sh .github/workflows/ci.yml scripts/test/test_canopen_dependencies.sh
```

---

### Task 4: Run offline cross qualification and record non-HIL evidence

**Files:**

- Modify: `third_party/README.md`
- Modify: `AGENTS.md`
- Modify: `README.md`
- Create: `docs/verification/P5_1_DEPENDENCY_QUALIFICATION.md`

**Interfaces:**

- Consumes: clean dependency pair, verified recursive source snapshot, existing locked cross image, validated RK3588 sysroot, and reviewed release sysroot lock.
- Produces: reviewable P5.1 qualification evidence; P5.2 may rely on the dependency paths and offline snapshot contract but not on any runtime CAN behavior.

- [ ] **Step 1: Update provenance and project status**

Record:

- canonical upstream URLs;
- both exact commits and Apache-2.0 licenses;
- nested submodule relationship;
- zero local patches and clean working trees;
- removal of the mixed snapshot;
- P5.1 complete only after all available required build gates pass;
- P5.2 remains the first slice that links CANopen sources.

Correct the current-state wording so historical CANopen evidence is not presented as implementation in this Linux repository. Do not rewrite the Phase 0 integrity manifest; it remains historical evidence.

- [ ] **Step 2: Run Debug cross qualification with the existing network-disabled build**

```bash
rtk env \
  ROBOT_CONTROL_SYSROOT=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204.lock.json \
  ROBOT_CONTROL_PRESET=rk3588-debug \
  ./scripts/build/build_rk3588.sh
```

Expected: validated sysroot, Docker build with `--network none`, successful aarch64 artifacts and ELF audit.

- [ ] **Step 3: Run Release cross qualification**

The release build requires a clean source snapshot. Verify the cross-image lock
with `scripts/build/verify_cross_image.sh`, commit an intentional lock update,
and confirm `git status --short` is empty before the Release build. Never bypass
the clean-source gate.

When the worktree is clean, run:

```bash
rtk env \
  ROBOT_CONTROL_SYSROOT=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204 \
  ROBOT_CONTROL_SYSROOT_LOCK=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/locks/rk3588-ubuntu2204-a685ab13.json \
  ROBOT_CONTROL_PRESET=rk3588-release \
  ./scripts/build/build_rk3588.sh
```

Expected: network-disabled Release build, successful artifact publication, checksum verification, and ELF audit.

- [ ] **Step 4: Create the qualification evidence document**

Record exact source revision, dependency revisions, commands, exit codes, test counts, cross artifact paths/checksums, toolchain/sysroot identities, and classified failures. Add a `test_result` YAML record using the actual clean qualification revision. Its fixed fields are: schema version 1, empty artifact checksums, level `hil`, target `not-applicable-by-phase-scope`, command `not run: P5.1 links no CANopen source`, null start time with zero duration and timeout, attempts 0, passed false, zero test counts, no classified failures, log `no HIL log; no target, physical CAN, drive, or vcan operation executed`, and requirement `P5.1-HIL-001`. Explain that `passed: false` means no HIL pass is claimed; it is not a product failure. No target address, credential, or secret may enter the document.

- [ ] **Step 5: Run final local audit**

```bash
rtk ./scripts/test/test_canopen_dependencies.sh
rtk cmake --preset host-test -S /home/gtc/Desktop/workspace/Linux_PROJ/robot_control
rtk cmake --build --preset host-test
rtk ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'
rtk ./scripts/test/test_phase1_scripts.sh
rtk ./scripts/test/test_sysroot_manifest.sh
rtk git diff --check
rtk git status --short --branch
rtk git diff -- docker/cross/image.lock
rtk git submodule status --recursive
```

Expected: all required checks pass; both submodules show exact commits without
`+`, `-`, or `U`; the working tree is clean; Debug and Release cross evidence
uses the same clean source revision.

- [ ] **Step 6: Commit documentation and qualification evidence**

Stage explicit paths only, excluding `docker/cross/image.lock`:

```bash
rtk git add third_party/README.md AGENTS.md README.md docs/verification/P5_1_DEPENDENCY_QUALIFICATION.md docs/plans/P5_1_DEPENDENCY_HIL_PLAN.md
rtk git commit -m "docs: record P5.1 dependency qualification" -- third_party/README.md AGENTS.md README.md docs/verification/P5_1_DEPENDENCY_QUALIFICATION.md docs/plans/P5_1_DEPENDENCY_HIL_PLAN.md
```

## Completion Gate

P5.1 is complete only when:

- the old mixed snapshot is gone;
- both exact commits and the nested relationship pass the verifier;
- wrong and dirty dependency states are rejected;
- recursive source snapshots contain both dependency levels;
- host build/tests and script/static regressions pass;
- RK3588 Debug and Release cross builds pass with Docker networking disabled;
- provenance/evidence documents are current;
- no third-party source is modified;
- no target-runtime, CAN, drive, deployment, or motion command was executed.

The completion gate closed on 2026-08-25 after the cross-image lock was verified
and committed, ShellCheck passed, and a clean no-SocketCAN remediation batch
passed both RK3588 cross presets. The earlier isolated-`vcan` execution remains
recorded as a superseded process deviation.
