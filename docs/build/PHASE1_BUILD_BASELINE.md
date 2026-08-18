# Phase 1 Build and ABI Baseline

Status date: 2026-08-17.

## Implemented

- CMake 3.22-compatible root project with C++20, strict warnings, testing, and
  out-of-source presets.
- Host Debug/Test, RK3588 Debug/Release, and RK3588 native presets.
- Strict aarch64 toolchain file requiring an explicit sysroot.
- Read-only platform/ABI probe with no hardware access.
- Reproducible `rk3588-cross:phase1-20260814` construction from
  `docker/cross/Dockerfile`, an immutable Ubuntu base digest, a dated Ubuntu
  snapshot, and the complete `docker/cross/packages.lock`.
- Schema-2 image locking and verification of the local image ID, image labels,
  architecture, target triplet, compiler, CMake, Ninja, linker, embedded
  package lock, and installed-package manifest.
- Process-level transactional image publication: a random candidate tag is
  built and verified against a staged, fsynced lock before the canonical tag
  and lock are replaced. A global daemon/image-keyed writer lock serializes
  compliant publishers. Candidate verification failure leaves the previous
  canonical image and lock unchanged; caught publication failure restores
  prior state only when it still belongs to the current transaction.
- Optional clone-scoped development containers with read-only source, writable
  output and ccache mounts, disabled networking, dropped capabilities, and
  `no-new-privileges`. Managed, repository, clone, owner UID/GID, and
  container-contract labels prevent deletion or reuse of foreign same-name
  containers.
- Ephemeral `docker run --rm` cross builds that execute as the host UID/GID and
  mount the selected sysroot read-only at
  `/opt/robot-control/sysroot`.
- Deterministic source snapshots containing currently existing tracked and
  nonignored untracked files. Deleted tracked paths are excluded, and the
  image lock, recipe, package lock, CMake presets, and toolchain metadata are
  verified or hashed from the same snapshot used by the build.
- Fresh per-preset CMake/Ninja build trees for every cross-build invocation,
  preventing deterministic snapshot mtimes from validating stale objects;
  ccache remains the reusable compiler cache.
- Target manifest collection, sysroot synchronization, absolute-symlink
  normalization, and Ubuntu 22.04/aarch64 validation scripts.
- Staged sysroot synchronization: collection, normalization, manifest
  generation, and validation complete before the destination is replaced. A
  global exclusive lock keyed by the canonical destination and adjacent lock
  serializes compliant publishers from pre-publication inspection through
  cleanup.
- Relative checksums for target metadata plus a deterministic JSONL content
  manifest for `lib`, `usr/lib`, and `usr/include`; validation recomputes the
  content manifest before every cross build.
- External JSON sysroot identity locks binding the artifact ID, architecture,
  OS version, content manifest, and target metadata; cross builds require the
  lock and record its digest.
- Separate sysroot lock authority: synchronization always writes an adjacent
  generated lock, debug builds may consume it, and release builds require an
  explicit Git-tracked, clean reviewed lock below `sysroots/locks/`.
- Managed-destination markers and interruption-aware rollback for sysroot and
  artifact publication.
- Managed artifact markers plus an internal `rk3588-debug` /
  `rk3588-release` preset allowlist prevent replacement of foreign artifacts
  or metadata publication under an unexpected preset.
- ELF architecture/interpreter/dependency/RPATH audit.
- Checksummed build metadata generation.
- Negative tests proving an incomplete sysroot is rejected.

## Authoritative toolchain

Recorded in `docker/cross/image.lock`:

- lock schema: `2`;
- image: `rk3588-cross:phase1-20260814`;
- image ID:
  `sha256:900c5130bfe1bc1961d078addbcc40a05ae5f4b8730cdce4ae2f9e0e20088e43`;
- base image:
  `ubuntu:22.04@sha256:3b06811b2afd352be909dd088a004166d665dc76d38b13eada33522a9d915c6f`;
- Ubuntu snapshot: `20260814T000000Z`;
- Dockerfile frontend:
  `docker/dockerfile:1@sha256:ecfaec9ed6d810b56388c508f4121597bfbba70d41a6dfeee4d8cad5f295fc32`;
- Dockerfile SHA-256:
  `4ae59ecaffbae16fe258de09d59f7dc58b2046fd404ab39a6823d126577c3c2b`;
- package-lock SHA-256:
  `5bd92611e9f3857e545a4c54f0bf2ffe01c17a3ee0421bad159c686ddd3d81be`;
- compiler: Ubuntu GCC 11.4.0 cross compiler;
- CMake: 3.22.1;
- Ninja: 1.10.1;
- linker: GNU ld 2.38;
- installed-package manifest SHA-256:
  `e9932a8f509195f5707fba74037b3006cda11d4d23e9f15f9e15bc10f32f496c`.

The compiler container is not the target userspace. Its `/usr/aarch64-linux-gnu`
and Ubuntu libraries are not accepted as the release sysroot.

`./scripts/build/setup_cross_container.sh` creates or reuses a clone-scoped
development container. Its default name is `rk3588-dev-<clone-id>`, with the
12-character clone ID derived from the absolute checkout path; it is not a
repository-wide fixed container name. The container is labeled with
`robot-control.managed=true`, a repository-path digest, the clone ID, explicit
owner UID/GID, and a derived `robot-control.container-contract`. A same-name
container without the complete ownership contract is reported and left
untouched.
`./scripts/build/build_rk3588.sh` does not use this persistent container.

## Sysroot contract

The release sysroot must:

1. come from the actual release-equivalent RK3588 Ubuntu 22.04 root filesystem;
2. contain `/usr/include`, `/lib`, `/usr/lib`, and the aarch64 loader;
3. contain recorded OS/package/kernel/libc metadata with relative-name
   checksums in `.robot-control/manifest.sha256`;
4. contain `.robot-control/sysroot-content.jsonl` and its relative-name
   checksum, deterministically describing `lib`, `usr/lib`, and `usr/include`;
5. be stored outside Git at any suitable absolute host path;
6. be mounted read-only by the cross build;
7. pass `scripts/sysroot/validate_sysroot.sh`, including a freshly recomputed
   content-manifest comparison;
8. have an external JSON identity lock, selected by
   `ROBOT_CONTROL_SYSROOT_LOCK` or the default adjacent
   `<sysroot>.lock.json`, that is a regular non-symlink file.

The collection script reads target files and writes only to a host-side staging
directory. It does not install packages or alter target configuration. It
always writes the generated identity to the adjacent `<sysroot>.lock.json`;
the build-only `ROBOT_CONTROL_SYSROOT_LOCK` variable is rejected during sync
so a reviewed release lock cannot be overwritten accidentally. After
normalization, manifest generation, validation, and external-lock generation
pass, the script replaces only a previously managed destination. Caught
promotion failures restore the prior sysroot and lock when possible; otherwise
their transaction backups are preserved. A missing destination with an
existing adjacent lock is rejected as an orphaned publication instead of
silently overwriting that lock. A process-global lock keyed by the canonical
destination and adjacent lock serializes compliant publishers before they read
or replace either artifact. This process-level contract does not claim
filesystem power-loss atomicity for the tree and lock.

`sysroots/` is a convenient ignored location, not a mount or build
requirement. As of 2026-08-17, it contains documentation under
`sysroots/README.md` and `sysroots/locks/README.md`, but no actual target
sysroot or reviewed JSON lock is present in this checkout.

## Commands

Build and verify the cross image:

```bash
./scripts/build/build_cross_image.sh --update-lock
./scripts/build/verify_cross_image.sh
```

Run the current host and script checks:

```bash
./scripts/build/build_host.sh
./scripts/test/test_phase1_scripts.sh
./scripts/test/test_sysroot_manifest.sh
./scripts/build/setup_cross_container.sh
```

Collect and use a release-equivalent target sysroot:

```bash
sysroot_dir="$HOME/.cache/robot-control/sysroots/rk3588-ubuntu2204"
./scripts/sysroot/sync_from_target.sh <ssh-alias> "$sysroot_dir"

ROBOT_CONTROL_SYSROOT="$sysroot_dir" \
ROBOT_CONTROL_PRESET=rk3588-debug \
  ./scripts/build/build_rk3588.sh
```

For a release build, first review and commit the generated lock under
`sysroots/locks/`, then select it explicitly:

```bash
ROBOT_CONTROL_SYSROOT="$sysroot_dir" \
ROBOT_CONTROL_SYSROOT_LOCK="$PWD/sysroots/locks/<reviewed-lock>.json" \
ROBOT_CONTROL_PRESET=rk3588-release \
  ./scripts/build/build_rk3588.sh
```

The cross build validates the sysroot, verifies the locked image, runs an
ephemeral network-disabled container as the host UID/GID, audits the ELF, and
writes:

```text
out/artifacts/rk3588-release/
├── robot-control-platform-probe
├── build-metadata.json
└── SHA256SUMS
```

## Verification on 2026-08-14

The following commands passed locally:

```bash
./scripts/build/build_host.sh
find scripts -type f -name '*.sh' -print0 | xargs -0 bash -n
find scripts -type f -name '*.sh' -print0 | xargs -0 shellcheck
PYTHONPYCACHEPREFIX=/tmp/robot-control-pycache-doc-verification \
  python3 -m py_compile \
    scripts/sysroot/generate_content_manifest.py \
    scripts/sysroot/write_sysroot_lock.py
./scripts/test/test_phase1_scripts.sh
./scripts/test/test_sysroot_manifest.sh
./scripts/build/build_cross_image.sh --update-lock
./scripts/build/verify_cross_image.sh
./scripts/build/setup_cross_container.sh

cmake -S . -B out/build/host-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build out/build/host-sanitize
ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir out/build/host-sanitize --output-on-failure
```

Results:

- host configure/build and 5/5 CTest tests;
- Bash syntax validation;
- ShellCheck;
- Python byte-code compilation for the sysroot manifest and lock generators;
- `scripts/test/test_phase1_scripts.sh`, including deleted-tracked-file source
  snapshots, frozen-snapshot metadata hashes, clean cross-build-directory,
  release-lock policy, clone/container ownership, managed artifact and preset
  policy, sync early-rejection cleanup, positive metadata publication,
  failed-publication rollback, candidate-image verification failure, final
  canonical-verification image/lock rollback, lock-promotion failure rollback,
  caller-supplied lock-symlink rejection, and concurrent image-publisher
  serialization regressions;
- `scripts/test/test_sysroot_manifest.sh`, including concurrent sysroot
  publisher serialization and forged inherited-lock rejection;
- `scripts/build/build_cross_image.sh --update-lock`, producing image ID
  `sha256:900c5130bfe1bc1961d078addbcc40a05ae5f4b8730cdce4ae2f9e0e20088e43`
  and refreshing the schema-2 lock with process-level transactional rollback
  through final canonical verification. Docker tags and filesystem locks are
  not claimed to be jointly atomic across host crashes or power loss;
- `scripts/build/verify_cross_image.sh`;
- `scripts/build/setup_cross_container.sh`, creating or reusing
  `rk3588-dev-7ee2b3886f5c` with the documented mount and security contract.
- ASan/UBSan configure/build and 5/5 CTest tests with LeakSanitizer disabled
  only for the current ptrace-based execution surface;
- LLVM 22.1.8 `clang-tidy` over the Phase 3 project sources with analyzer,
  bugprone, performance, and portability checks, excluding the repository's
  accepted `#pragma once` style and third-party headers;
- `clang-format --dry-run --Werror` for modified project C/C++ sources and
  `git diff --check`.

The retained full image-construction log is
`out/logs/docker-cross-build-20260814-final.log`. The current post-review
publication/verification path was rerun against the real Docker daemon with a
cache-backed build and is retained as
`out/logs/docker-cross-build-20260814-transaction.log`; `out/` is local
evidence and is not committed.

The 2026-08-14 verification did not include a real aarch64 cross build, ELF
audit, artifact transfer, or target smoke test because no actual sysroot is
currently available under `sysroots/` or otherwise supplied to this checkout.

## Workspace review on 2026-08-17

The A-D repair pass reran the locally available checks after making the sysroot
manifest fixture independent of the caller's umask:

```bash
umask 0002; ./scripts/test/test_sysroot_manifest.sh
umask 0022; ./scripts/test/test_sysroot_manifest.sh
./scripts/test/test_phase1_scripts.sh
./scripts/build/build_host.sh
cmake --list-presets=all
git diff --check
```

Results:

- the sysroot manifest regression suite passed under both umask values;
- Phase 1 negative-path script tests passed;
- host configure/build and 5/5 CTest tests passed;
- all project shell scripts passed `bash -n`;
- both sysroot Python tools compiled successfully without writing byte code;
- the Dockerfile and package-lock SHA-256 values still match the schema-2 image
  lock;
- local `.codex/` task state is ignored and excluded from deterministic source
  snapshots.

Docker, ShellCheck, and Hadolint are unavailable in the 2026-08-17 execution
environment. Therefore the locked image, container runtime contract, and
Dockerfile lint were not rerun in this pass. No actual target sysroot or
reviewed release lock is available, so aarch64 debug/release builds and ELF
auditing remain pending external prerequisites.

## Docker and target-sysroot verification on 2026-08-18

The previously unavailable checks were completed after Docker, ShellCheck,
Hadolint, and a read-only sysroot collected from the authorized RK3588 target
became available. This verification used:

- Docker client and server version 29.1.3;
- locked image `rk3588-cross:phase1-20260814` with immutable image ID
  `sha256:682f8ed4409ffc8b68053d325ddaf98870e5e1d86f250141acc50eee3ef729e8`;
- sysroot content digest
  `a685ab13c6e2087dde9ec29e0f6c18a49cf0af477692f2e3a2b0b8b6e5c39911`;
- reviewed release lock
  `sysroots/locks/rk3588-ubuntu2204-a685ab13.json`.

Results:

- locked-image runtime verification passed;
- all project shell scripts passed ShellCheck and the cross Dockerfile passed
  Hadolint;
- RK3588 Debug and Release cross builds completed against the real target
  sysroot;
- both ELF audits verified the aarch64 interpreter, `NEEDED` dependencies,
  GLIBC/GLIBCXX/CXXABI symbol versions, and absence of RPATH/RUNPATH;
- both artifact metadata records identify clean revision
  `3f7d794808b9b77b852fe7838a3a67b37bd77db5` and a 593-file source snapshot.

The resulting local evidence is retained below
`out/artifacts/rk3588-debug/` and `out/artifacts/rk3588-release/`; `out/` is not
committed. Target smoke testing remains pending and no motion-producing or
persistent target operation was performed.

## Historical cross and target evidence

The following evidence was collected on 2026-07-31 and is retained as the
Phase 1 ABI baseline. It was not reverified on 2026-08-14:

- board-derived Ubuntu 22.04 sysroot collection (2.6 GiB, 912 installed
  package records);
- aarch64 cross configure/build with GCC 11.4.0;
- ELF interpreter, NEEDED library, GLIBC/GLIBCXX/CXXABI symbol-version and
  RPATH/RUNPATH audit against the collected sysroot;
- target artifact ownership remains UID/GID 1000;
- artifact SHA-256 matches before and after SSH transfer;
- probe execution on RK3588 reports Linux 6.1.84 and `machine=aarch64`;
- target `ldd` resolves the executable through
  `/lib/ld-linux-aarch64.so.1`, target `libstdc++`, `libc`, `libm`, and
  `libgcc_s`;
- `git diff --check`.

## Collected target identity

The 2026-07-31 read-only target probing and sysroot manifest recorded:

- target: RK3588 at the separately managed deployment inventory address;
- architecture: arm64/aarch64;
- OS: Ubuntu 22.04.5 LTS;
- kernel: Linux 6.1.84;
- glibc: 2.35;
- unprivileged development account UID: 1000.

The target password is not stored in the repository, build metadata, sysroot
manifest, shell history generated by project scripts, or documentation.

## Remaining release-toolchain gap

The Dockerfile, Ubuntu snapshot, base digest, complete package lock, recipe
digests, local image ID, tool versions, and installed-package manifest are
locked, so the earlier image-reconstruction provenance gap is closed.

Release publication still needs an explicit registry/export policy and, if the
image is published, the registry OCI manifest digest. This document does not
claim that `rk3588-cross:phase1-20260814` has been pushed to a registry.

No CAN, UART, motor, device-tree, network, or persistent target state change is
part of this phase.
