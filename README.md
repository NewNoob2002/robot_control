# robot_control

Production-oriented Linux motion-control middleware for an RK3588 robot.

The daemon will own SBUS input, command arbitration, robot-level safety,
CANopen/CiA402 drive control, diagnostics, and safe lifecycle behavior. ROS2 is
an adapter and is not the low-level safety authority.

## Project status

Phase 1 build infrastructure is in progress. The repository has a host-native
CMake probe/test build and a strict RK3588 cross-build path which requires a
validated Debian 11 aarch64 sysroot. Motion-control product code has not started.

Read these documents before implementation:

- [`AGENTS.md`](AGENTS.md)
- [implementation plan](.omx/plans/rk3588-motion-control-middleware-plan.md)
- [architecture decisions](docs/decisions/)
- [legacy behavioral contract](docs/architecture/LEGACY_BEHAVIOR_BASELINE.md)
- [third-party provenance](third_party/README.md)

## Phase 1 commands

Host build and tests:

```bash
./scripts/build/build_host.sh
./scripts/test/test_phase1_scripts.sh
```

The existing toolchain is recorded in `docker/cross/image.lock` and is reused:

```bash
docker ps --filter name=rk3588-dev
```

Collect a sysroot from an authorized target and cross-build:

```bash
./scripts/sysroot/sync_from_target.sh <ssh-target> ./sysroots/rk3588-debian11
ROBOT_CONTROL_SYSROOT="$PWD/sysroots/rk3588-debian11" \
  ./scripts/build/build_rk3588.sh
```

`rk3588-dev` already mounts the host repository at `/workspace`. Build commands
execute as the host UID/GID, so output remains host-owned. The sysroot must be
under the ignored `sysroots/` directory so the running container can see it.
The Ubuntu-container filesystem is deliberately rejected as a target sysroot.

## Safety

Do not move hardware, change persistent drive parameters, alter target network
or device-tree configuration, or deploy to production without explicit
authorization and the applicable hardware-test preflight.
