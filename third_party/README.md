# Third-Party Dependency Provenance

Status date: 2026-10-09.

Normal builds must not fetch mutable branches. A dependency is accepted only
after its upstream URL, immutable commit, license, integration method, and local
patch status are recorded here and in the applicable ADR.

## Development-only ROS2 environment

The official ROS Humble ros-base Jammy amd64 image is pinned by platform digest
in [docker/ros2/image.lock](../docker/ros2/image.lock). Purpose, upstream source
revision, package versions, per-package license locations, architecture limits
and the non-root colcon smoke result are recorded in
[its README](../docker/ros2/README.md). It is a host development environment only,
with no new core CMake dependency or target ROS2 qualification claim. The
official image layers are reused without local patches or package additions.

A separate arm64 official Humble runtime was extracted into a private RK3588
user directory for device-free IPC/DDS checks. It is pinned by its platform digest
and retains package/version/license evidence in
[the board baseline](../docs/verification/IPC_BOARD_BASELINE.md).
That extraction changed no system packages and does not change the amd64 development
lock or add a core link dependency. Production ROS deployment or drive HIL is not claimed.

A separate host RViz environment reuses OSRF's Humble desktop amd64 image at
`sha256:fb07245b32187d74350be25323d8ad2f8ca5c25c325759911a1eff2267a49c1e`, without
local image patches or apt additions. Its purpose, observed RViz version, package
license locations, X11/network settings and actual GUI smoke are documented in
[docker/ros2/README.md](../docker/ros2/README.md). It is independent of the existing
ros-base test image and core targets. The operator has installed native board
ros-base using official ros2-apt-source1.3.0; native imports and the IPC/DDS
regression pass. Exact package versions and archived identity/test evidence are
recorded in [the board baseline](../docs/verification/IPC_BOARD_BASELINE.md).

## Removed snapshots

### Historical CANopenNode snapshot

- Former path: `components/CANopenNode` (removed in P5.1)
- Upstream: <https://github.com/CANopenNode/CANopenNode>
- License: Apache-2.0; the historical license file is recorded by
  `docs/baseline/MANIFEST.sha256` (current upstream copy:
  `components/CANopenLinux/CANopenNode/LICENSE`)
- Local file count at baseline: 71
- Baseline content digest:
  `1e3e5e8ffd43dfe4df8e40345cab3fe51f0beb757c89ac88c63b20f2ac3879d5`
  (SHA-256 over the sorted list of per-file SHA-256 records)
- Version status: **checksum-pinned vendored snapshot; upstream commit remains
  unresolved**

The historical snapshot could not be attributed to one upstream commit from the
available files. For example:

- local `CANopen.c` exactly matches upstream commit
  `8c09a433efdb85c15f141e177127d481732c66ac`;
- local `doc/CHANGELOG.md` exactly matches upstream commit
  `f41bfdcb5d0d432455e5d4df0a16cd5520a8fd57`.

Those files resolve to different commits. P5.1 removed the directory instead
of preserving a second CANopenNode copy.

### Historical EasyLogger snapshot

- Former path: `components/EasyLogger` (removed and replaced by spdlog v1.17.0)
- Upstream: <https://github.com/armink/EasyLogger>
- Historical license: MIT; the former file is preserved by the Git baseline,
  not present at the removed working-tree path.

## Installed dependencies

### spdlog

- Path: `components/spdlog`
- Upstream: <https://github.com/gabime/spdlog.git>
- Immutable commit / Tag: `79524ddd08a4ec981b7fea76afd08ee05f83755d` (`v1.17.0`)
- License: MIT (`components/spdlog/LICENSE`)
- Integrated via Git submodule at `components/spdlog` and built via `add_subdirectory`.
- Static/private implementation dependency of `service/logging`; no ROS dependency.
- Target availability: built with the locked GCC11.4 runner against the actual
  RK3588 Ubuntu22.04 sysroot. Local patches: none.
- Test impact: structured formatting, 512-byte record limits, pipe/socket/file
  failure diagnostics, app rotation/mirroring and existing Phase3 ELF audit.

### CANopen dependency pair

P5.1 installs the pair as recursive Git submodules:

| Dependency | Path | Canonical upstream | Immutable commit |
|---|---|---|---|
| CANopenLinux | `components/CANopenLinux` | <https://github.com/CANopenNode/CANopenLinux.git> | `f1348d4072cdabea4c3435a13c721ac29ab4cc91` |
| CANopenNode | `components/CANopenLinux/CANopenNode` | <https://github.com/CANopenNode/CANopenNode.git> | `ef9ac3a2279e34855a20c787fc1bc48bc995ec22` |

Both use Apache-2.0. The pair is chosen together because CANopenLinux pins that
exact CANopenNode commit as its submodule, eliminating an inferred API match.
Both working trees are clean and carry zero local patches. Normal builds verify
the root gitlink, nested gitlink, canonical URLs, exact checkouts, and clean
trees before configuration; P5.1 does not add either dependency to CMake.

## Integration policy

1. Retain CANopenLinux as the only top-level CANopen dependency; its nested
   CANopenNode gitlink is the compatibility authority.
2. Initialize both levels recursively before normal offline builds.
3. Do not edit third-party files directly. Carry necessary changes as minimal,
   documented patches and attempt upstream compatibility first.
4. CI and release builds operate without network access after dependencies are
   checked out.
5. Generate a license/SBOM report before the first release.
