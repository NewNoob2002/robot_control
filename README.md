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
Phase 4 SocketCAN work provides a policy-free Classical CAN frame codec, a
move-only nonblocking socket lifecycle with explicit kernel filter/error-mask
configuration, and complete-frame send/receive with monotonic timeout and
cancellation. Receive observations also preserve optional raw kernel
nanosecond timestamps and RX queue overflow counters for diagnostics. The
passive `robot-control-can-probe` is bounded and emits structured observations.
A capability-aware isolated runner provides real bidirectional `vcan` frame and
raw-filter-isolation coverage plus a bounded queue-pressure check that records
a nonzero raw cumulative `SO_RXQ_OVFL` counter, complete raw CAN error-frame
preservation, and interface down/up with explicit endpoint reopen evidence,
while explicitly skipping unsupported hosts. RK3588 passive target validation
is complete, including ordinary-user deadline/SIGTERM behavior and raw
ID/DLC/payload/timestamp/overflow preservation under external traffic. Phase 5
P5.1 through P5.6 are complete: the pinned CANopen dependency pair, minimal
stack, single-owner lifecycle, immutable observations, managed-vcan receive
evidence, and isolated Debug-only read-only commissioning path pass their host,
sanitizer, static, and RK3588 cross gates. P5.7 local qualification, target
deployment, bounded deadline/SIGTERM, cleanup, and independent zero-transmit
evidence pass. A final error-enabled HIL run preserved the exact node-1
read-only SDO 0x601 request and 0x581 response in the target raw capture; the
normal observer published the response as the expected `sdo_rejected` raw
observation. Target RXF/RXMF advanced by two, TX remained zero, no error frame
was observed, and cleanup passed. Phase 5 is complete.
See
docs/verification/PHASE5_CANOPEN_BASELINE.md.
Phase 6 checkpoint, September 11, 2026: synchronous packed-target feedback and
single-RPDO left/right trials pass, with operator-confirmed normal stopping.
TPDO1 carries both statuswords and packed speeds; the temporary RPDO1 carries
the common controlword and packed targets, with its original mapping restored
after each trial. Host and sanitizer qualification suites pass 60/60 tests.
The revised synchronous stop/loss artifact passes cross-build and target vcan
checks, but its physical requalification is blocked by execution-approval
timeouts before process creation. Earlier NMT Stop, Shutdown, Disable Voltage
and Quick Stop evidence remains historical; Phase 6 is not complete.
See the [synchronous PDO repair record](docs/verification/P6_SYNC_PACKED_PDO_REPAIR.md).
See the [Phase 6 checkpoint](docs/verification/PHASE6_CHECKPOINT.md) for current
status and the [evidence index](docs/verification/evidence/README.md) for accepted
trials and archived history. The qualification executor remains Debug-only and
default-OFF; production motion, persistent configuration and loaded operation
remain outside this checkpoint.
GitHub Actions selects tests from the complete push diff or PR merge-base diff
on development branches (pushes to main and codex/** are enabled). Branch names
never suppress tests for changed dependencies:

| Change scope | CI checks |
| --- | --- |
| SBUS, UART, SBUS tests or P9 evidence | SBUS Debug/Release/ASan+UBSan: contracts, PTY, observer, archived replay, offline control cycle and Linux platform tests; input/sbus and platform/linux/uart code also run runtime/vcan closure |
| CAN/CANopen, Phase6 tools/tests or P6 evidence | CAN/Phase6 Debug and ASan+UBSan regression, evidence checks and mandatory vcan; shared CAN transport changes also run PDO runtime |
| P8-R runtime or P10 application/control/tests/evidence | PDO runtime, offline control cycle and domain Debug/ASan+UBSan contracts plus managed vcan including PTY-to-RPDO closure; no Phase6 suite unless shared dependencies change |
| Mixed subsystem changes | All affected scoped suites |
| README/AGENTS or ordinary documentation only | Selection regression and CI result; builds skipped |
| Shared code/build configuration, CI itself or unclassified code | Full host, Phase6 and PDO runtime regression, including SBUS |
| Push/PR to main, merge queue, manual run, new branch or unavailable base history | Full regression |

Full host validation retains Phase1/sysroot script regressions, ShellCheck,
Hadolint and Python syntax checks on Ubuntu 22.04. The stable **CI result** job
fails if a selected suite fails, is cancelled or unexpectedly skipped; it is
the aggregate check to use for branch protection. Selection rules and executable
regressions live in scripts/ci/select_scope.py and scripts/test/test_ci_scope.py.
Real RK3588 cross builds remain local because they require the checksum-locked
sysroot collected from the authorized target and the locally locked toolchain
image; workflow routing does not replace milestone hardware acceptance.

Read these documents before implementation:

- [`AGENTS.md`](AGENTS.md)
- [Phase 0 repository baseline](docs/baseline/README.md)
- [Phase 1 build and ABI baseline](docs/build/PHASE1_BUILD_BASELINE.md)
- [Phase 2 domain baseline](docs/verification/PHASE2_DOMAIN_BASELINE.md)
- [Phase 3 Linux platform baseline](docs/verification/PHASE3_LINUX_PLATFORM_BASELINE.md)
- [Phase 4 SocketCAN foundation plan](docs/plans/PHASE4_SOCKETCAN_FOUNDATION.md)
- [Phase 5 non-actuating CANopen integration plan](docs/plans/PHASE5_CANOPEN_INTEGRATION.md)
- [Phase 6 ZLAC8015D drive qualification plan](docs/plans/PHASE6_ZLAC8015D_QUALIFICATION.md)
- [Phase 9 SBUS → P8 runtime completion → P10 integration plan](docs/plans/PHASE9_SBUS_AND_INTEGRATION.md) — next development from the September 15 archive; Phase 6 remains open.
- [P9.2 SBUS UART and receive-only observer](docs/verification/P9_2_SBUS_UART_BASELINE.md) — accepted within the UART/receive-only scope.
- [P9.3 SBUS health, mapping and snapshots](docs/verification/P9_3_SBUS_SOURCE_BASELINE.md) — CLOSED within receive-only input scope; calibrated Source HIL passed, with the first failed attempt preserved.
- [P8-R PDO runtime and drive binding](docs/verification/P8_R_RUNTIME_BASELINE.md) — software/vcan accepted; physical layout and remote control remain P10 work.
- [P10.1 offline control cycle](docs/verification/P10_1_CONTROL_CYCLE_BASELINE.md) — SBUS snapshots, arbitration, safety and guarded PDO output; no hardware entry point.
- [P10.2 vcan control loop](docs/verification/P10_2_CONTROL_LOOP_BASELINE.md) — PTY to actual RPDO/independent feedback, failure injection and bounded shutdown reporting; physical acceptance remains P10.3.
- [P10.3 HIL checkpoint](docs/verification/P10_3_HIL_CHECKPOINT.md) — receive-only and stationary TPDO2 prerequisites pass; actual zero-only ControlLoop artifact is offline-verified/staged, awaiting fresh powered readiness. Full-chain physical acceptance remains open.
- [P6.1 contract, fixture, and read-only baseline](docs/verification/P6_1_CONTRACT_FIXTURE_READ_ONLY_BASELINE.md)
- [P6.2 ZLAC protocol semantics baseline](docs/verification/P6_2_ZLAC_PROTOCOL_SEMANTICS_BASELINE.md)
- [P6.3 bounded qualification executor baseline](docs/verification/P6_3_BOUNDED_QUALIFICATION_EXECUTOR_BASELINE.md)
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

If downloads require a local HTTP proxy, configure the Docker daemon for image
pulls and export `HTTP_PROXY`, `HTTPS_PROXY`, and `NO_PROXY` for the build client.
The script forwards these standard proxy arguments by name. When the proxy is
bound to host loopback, use `ROBOT_CONTROL_BUILD_NETWORK=host` for image creation;
normal middleware cross builds still run with networking disabled. For example,
after setting the proxy environment:

```bash
ROBOT_CONTROL_BUILD_NETWORK=host ./scripts/build/build_cross_image.sh --update-lock
```

The September 16 restoration retained the pinned base, APT snapshot and full
package manifest, verified GCC 11.4/CMake 3.22.1, and refreshed only the local
image ID in `docker/cross/image.lock`. Docker buildx is required.

For independent host ROS2 work, the pinned **Humble / Ubuntu 22.04 amd64** image
is available as `robot-control-ros2:humble-20260916`. See the
[ROS2 development image instructions](docker/ros2/README.md) for restore, non-root
shell and the successful colcon smoke check. It is not an aarch64 cross image.

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
