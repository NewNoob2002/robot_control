# Phase 1 Build and ABI Baseline

Status date: 2026-07-31.

## Implemented

- CMake 3.22-compatible root project with C++20, strict warnings, testing, and
  out-of-source presets.
- Host Debug/Test, RK3588 Debug/Release, and RK3588 native presets.
- Strict aarch64 toolchain file requiring an explicit sysroot.
- Read-only platform/ABI probe with no hardware access.
- Reuse of the existing `rk3588-dev` container and immutable image-ID check.
- Host UID/GID execution inside the container to avoid root-owned output.
- Target manifest collection, sysroot synchronization, absolute-symlink
  normalization, and Debian 11/aarch64 validation scripts.
- ELF architecture/interpreter/dependency/RPATH audit.
- Checksummed build metadata generation.
- Negative tests proving an incomplete sysroot is rejected.

## Authoritative toolchain

Recorded in `docker/cross/image.lock`:

- image: `rk3588-cross:latest`;
- image ID:
  `sha256:14cae4e1ab5aa8ee611e1efe4d20408bce17734b52297b9fa2c9c12a1742f677`;
- running container: `rk3588-dev`;
- container mount: host repository to `/workspace`;
- compiler: Ubuntu GCC 11.4.0 cross compiler;
- CMake: 3.22.1;
- Ninja: 1.10.1.

The compiler container is not the target userspace. Its `/usr/aarch64-linux-gnu`
and Ubuntu libraries are not accepted as the release sysroot.

## Sysroot contract

The release sysroot must:

1. come from the actual release-equivalent RK3588 Debian 11 root filesystem;
2. contain `/usr/include`, `/lib`, `/usr/lib`, and the aarch64 loader;
3. contain recorded OS/package/kernel/libc metadata;
4. live under the ignored repository `sysroots/` directory when the existing
   container is reused;
5. be treated read-only by the build;
6. pass `scripts/sysroot/validate_sysroot.sh`.

The collection script reads target files and writes only to the host output
directory. It does not install packages or alter target configuration.

## Commands

```bash
./scripts/build/build_host.sh
./scripts/test/test_phase1_scripts.sh

./scripts/sysroot/sync_from_target.sh <ssh-alias> ./sysroots/rk3588-debian11

ROBOT_CONTROL_SYSROOT="$PWD/sysroots/rk3588-debian11" \
  ./scripts/build/build_rk3588.sh
```

The cross build verifies the container image ID, configures and builds as the
host UID/GID, audits the ELF, and writes:

```text
out/artifacts/rk3588-release/
├── robot-control-platform-probe
├── build-metadata.json
└── SHA256SUMS
```

## Current verification evidence

Passed locally:

- host configure/build with GNU C++ 15.2.0;
- two CTest probe tests;
- invalid-sysroot rejection in both CMake and the validator;
- JSON and shell syntax validation;
- existing container image-ID match;
- CMake/GCC/Ninja version check inside `rk3588-dev`;
- host UID/GID write access to `/workspace/out`;
- `git diff --check`.

The existing local image is pinned by image ID for this workstation, but it
does not yet have an independently archived Dockerfile/base-digest provenance
chain. Export or reconstruction provenance is required before release CI can
reproduce it on another host; this plan intentionally does not rebuild the
image because the project owner supplied the existing GCC 11 image.

## External validation still required

The following cannot be claimed until an authorized target SSH alias or rootfs
artifact is available:

1. collection of the actual Debian 11 RK3588 sysroot;
2. successful cross-link and ELF dependency resolution against that sysroot;
3. execution of the probe on RK3588;
4. confirmation that reported `machine` is `aarch64` and runtime loader/library
   resolution succeeds.

No CAN, UART, motor, device-tree, network, or persistent target state change is
part of this phase.
