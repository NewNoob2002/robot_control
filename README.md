# robot_control

Production-oriented Linux motion-control middleware for an RK3588 robot.

The daemon will own SBUS input, command arbitration, robot-level safety,
CANopen/CiA402 drive control, diagnostics, and safe lifecycle behavior. ROS2 is
an adapter and is not the low-level safety authority.

## Project status

Phase 3 provides Linux mechanism adapters for descriptor ownership, poll-based
waiting, absolute monotonic timing, synchronous termination events, raw UART,
and throttled structured logging through EasyLogger. These adapters contain no
motion policy and are covered by pipe, PTY, signal, timing, sanitizer, and
cross-build tests.
Phase 4 SocketCAN work provides a policy-free Classical CAN frame codec and a
move-only nonblocking socket lifecycle with explicit kernel filter/error-mask
configuration. Frame I/O and metadata, `vcan`, passive target capture, CANopen,
and motion-producing integration remain pending.
The GitHub Actions host CI baseline runs the host build and CTest suite, Phase
1 and sysroot-manifest script regressions, ShellCheck, Hadolint, and Python
syntax checks on Ubuntu 22.04. Real RK3588 cross builds are intentionally not
run on public runners because they require the checksum-locked sysroot
collected from the authorized target and the locally locked toolchain image.

Read these documents before implementation:

- [`AGENTS.md`](AGENTS.md)
- [Phase 0 repository baseline](docs/baseline/README.md)
- [Phase 1 build and ABI baseline](docs/build/PHASE1_BUILD_BASELINE.md)
- [Phase 2 domain baseline](docs/verification/PHASE2_DOMAIN_BASELINE.md)
- [Phase 3 Linux platform baseline](docs/verification/PHASE3_LINUX_PLATFORM_BASELINE.md)
- [Phase 4 SocketCAN foundation plan](docs/plans/PHASE4_SOCKETCAN_FOUNDATION.md)
- [architecture decisions](docs/decisions/)
- [legacy behavioral contract](docs/architecture/LEGACY_BEHAVIOR_BASELINE.md)
- [third-party provenance](third_party/README.md)

## Phase 1 commands

Host build and tests:

```bash
./scripts/build/build_host.sh
./scripts/test/test_phase1_scripts.sh
./scripts/test/test_sysroot_manifest.sh
```

Build or verify the checksum-pinned cross-toolchain image:

```bash
./scripts/build/build_cross_image.sh --update-lock
./scripts/build/verify_cross_image.sh
```

`docker/cross/image.lock` schema 2 records the Ubuntu snapshot, base image
digest, Dockerfile and package-lock digests, local image ID, tool versions, and
installed-package manifest digest. The current local tag is the versioned
`rk3588-cross:phase1-20260814`; the immutable image ID remains the build
authority. Lock refreshes use process-level transactional publication: the
build is first published to a random candidate tag, the candidate is verified
against a staged lock, and only then are the canonical tag and lock replaced.
Caught failures through final canonical verification restore the previous
canonical image and lock. Publishers are serialized by a global lock keyed by
the Docker daemon ID and canonical image reference, and rollback refuses to
overwrite image or lock state changed outside its transaction. This does not
claim cross-subsystem atomicity for a host crash or power loss spanning the
Docker tag store and filesystem lock.

For an optional persistent development shell, create or reuse the
clone-scoped container:

```bash
./scripts/build/setup_cross_container.sh
```

The default name is `rk3588-dev-<clone-id>`, where the script derives the
12-character clone ID from the absolute checkout path. The script mounts the
source tree read-only, mounts `out/` and the ccache directory writable, disables
networking, drops all capabilities, and enables `no-new-privileges`. Reuse and
replacement require the expected managed, repository, clone, owner UID/GID, and
container-contract labels. A same-name container with foreign or unknown
ownership is left untouched; a legacy managed container without explicit owner
labels is migrated only when its complete contract already matches the current
user and checkout.

The actual cross build does not depend on that persistent container. It uses
an ephemeral `docker run --rm` invocation and mounts a validated, externally
locked sysroot read-only at `/opt/robot-control/sysroot`. The container builds
from a deterministic snapshot of currently existing tracked and nonignored
untracked source files; image-lock verification and build metadata hashes are
resolved from that same snapshot rather than the mutable live checkout.

Collect a sysroot from an authorized target to any suitable host path, then
cross-build:

```bash
sysroot_dir="$HOME/.cache/robot-control/sysroots/rk3588-ubuntu2204"
./scripts/sysroot/sync_from_target.sh <ssh-target> "$sysroot_dir"
ROBOT_CONTROL_SYSROOT="$sysroot_dir" \
ROBOT_CONTROL_SYSROOT_LOCK="$sysroot_dir.lock.json" \
ROBOT_CONTROL_PRESET=rk3588-debug \
  ./scripts/build/build_rk3588.sh
```

The sync script builds and validates a staging tree before replacing the
requested destination. It refuses to replace an unmanaged directory, marks
managed outputs, and always writes the adjacent external JSON identity lock;
`ROBOT_CONTROL_SYSROOT_LOCK` is build-only and cannot redirect sync output.
Concurrent compliant publishers for the same canonical destination and
adjacent lock are serialized by a global exclusive lock held from initial
publication inspection through cleanup, preventing cross-transaction
tree/lock pairing. This is process-level serialization with
interruption-aware rollback, not a claim of filesystem power-loss atomicity.
The build revalidates target metadata checksums, a deterministic content
manifest covering `lib`, `usr/lib`, and `usr/include`, and the external lock.
`rk3588-debug` may use the adjacent generated lock. `rk3588-release` requires
an explicit, Git-tracked, clean reviewed lock below `sysroots/locks/`; see
[`sysroots/locks/README.md`](sysroots/locks/README.md). The Ubuntu
compiler-container filesystem is deliberately rejected as a target sysroot.
External locks must be regular non-symlink files. Synchronization also rejects
an orphaned adjacent lock when its sysroot destination is absent.

Each cross build removes only its validated preset build directory before
configuration. This is intentional: deterministic source snapshots normalize
file mtimes, so reusing an older Ninja object tree could associate stale
objects with a new source attestation. Compiler output reuse remains available
through ccache.

As of 2026-08-18, the locked image has been runtime-verified and this checkout
has completed Debug and Release aarch64 cross builds against the real target
sysroot. Both artifacts passed interpreter, dependency, symbol-version, and
RPATH/RUNPATH ELF audits. The sysroot itself remains local and ignored; only
its reviewed release identity lock is versioned. Target smoke testing remains
pending. Detailed evidence is recorded in the
[Phase 1 baseline](docs/build/PHASE1_BUILD_BASELINE.md).

## Phase 3 verification

```bash
./scripts/build/build_host.sh
```

Detailed sanitizer, static-analysis, and boundary-audit evidence is recorded
in
[`docs/verification/PHASE3_LINUX_PLATFORM_BASELINE.md`](docs/verification/PHASE3_LINUX_PLATFORM_BASELINE.md).

When a validated release-equivalent sysroot is available, rerun the cross
build separately:

```bash
ROBOT_CONTROL_SYSROOT="/absolute/path/to/rk3588-ubuntu2204" \
ROBOT_CONTROL_SYSROOT_LOCK="$PWD/sysroots/locks/<reviewed-lock>.json" \
ROBOT_CONTROL_PRESET=rk3588-release \
  ./scripts/build/build_rk3588.sh
```

## Safety

Do not move hardware, change persistent drive parameters, alter target network
or device-tree configuration, or deploy to production without explicit
authorization and the applicable hardware-test preflight.
