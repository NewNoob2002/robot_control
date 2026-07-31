# ADR-0006: RK3588 Ubuntu 22.04 Target Baseline

- Status: Accepted
- Date: 2026-07-31
- Supersedes: ADR-0003 target-distribution choice and ADR-0005 migration gate

## Context

The RK3588 was reimaged from Debian 11 to Ubuntu 22.04.5 LTS. Read-only target
probing confirmed arm64/aarch64, Linux 6.1.84, and glibc 2.35. The existing
cross container is also Ubuntu 22.04.5 with GCC 11.4.0, but its filesystem is
still not evidence of the board's installed userspace and vendor libraries.

## Decision

Ubuntu 22.04 arm64 is the target and release sysroot baseline. Collect and
version a sysroot from the actual release-equivalent board/rootfs. Continue to
use the existing Ubuntu 22.04 GCC 11 cross container, but mount the board-derived
sysroot read-only and audit every target ELF against it.

## Alternatives

- Continue enforcing Debian 11: rejected because it no longer matches deployed
  hardware.
- Link against the container's cross libraries without a board sysroot:
  rejected because container/toolchain packages may omit or differ from target
  runtime and vendor libraries.
- Compile only on the RK3588: retained as an optional smoke path, not the
  reproducible primary build.

## Consequences

- Toolchain, validation scripts, documentation, and CI names use Ubuntu 22.04.
- ROS2 Humble gains a Tier-1 Ubuntu 22.04 arm64 deployment path for Phase 13.
- Existing Debian-oriented ADRs remain as historical decision records.
- Each target image update requires a new sysroot manifest and ABI verification.

## Verification

- Sysroot metadata reports Ubuntu 22.04 and arm64.
- Cross-built probe uses the aarch64 loader and resolves every dependency in
  the board-derived sysroot.
- Probe runs on the RK3588 and reports `machine=aarch64`.

