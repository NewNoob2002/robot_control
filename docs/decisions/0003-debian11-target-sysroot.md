# ADR-0003: Debian 11 Target Sysroot

- Status: Accepted
- Date: 2026-07-31

## Context

The build image is Ubuntu 22.04 with an aarch64 cross compiler. The RK3588 runs
a vendor Debian 11 image. Linking against container libraries could introduce
newer GLIBC/GLIBCXX or incompatible board-library dependencies.

## Decision

Generate a relocatable, versioned sysroot from the actual release-equivalent
RK3588 Debian 11 root filesystem. The host checkout remains authoritative and
the sysroot is mounted read-only into a pinned Docker toolchain image.

The sysroot artifact is not stored in Git. Git stores:

- an allowlisted collection script;
- OS, architecture and `dpkg-query` manifest;
- archive SHA-256 and artifact identifier;
- normalized absolute-symlink/linker-script report;
- compiler and CMake toolchain configuration.

The toolchain must set `CMAKE_SYSROOT`, root-path modes, and sysroot-aware
pkg-config variables. ELF audits check interpreter, dynamic dependencies,
RPATH/RUNPATH, architecture, and GLIBC/GLIBCXX symbol requirements.

## Alternatives

- Use Ubuntu 22.04 container libraries: rejected as ABI unsafe.
- Generic Debian 11 arm64 sysroot only: useful for CI but insufficient as the
  release authority because the vendor image may contain board libraries.
- Compile only on the board: rejected as slow, less reproducible, and contrary
  to the host-authoritative workflow.

## Consequences

- Initial setup needs read access to a known target/rootfs.
- Sysroot artifacts need secure/versioned storage.
- Every target OS update requires a new manifest and compatibility validation.
- Generic Debian packages may supplement CI but not silently replace the
  release sysroot.

## Verification

- A cross-built probe executes on the target.
- ELF dependency resolution succeeds exclusively against the recorded sysroot.
- No build path or host library appears in target RPATH/RUNPATH.

