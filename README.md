# robot_control

Production-oriented Linux motion-control middleware for an RK3588 robot.

The daemon will own SBUS input, command arbitration, robot-level safety,
CANopen/CiA402 drive control, diagnostics, and safe lifecycle behavior. ROS2 is
an adapter and is not the low-level safety authority.

## Project status

Phase 0 baseline is in progress. No production implementation or working root
build exists yet.

Read these documents before implementation:

- [`AGENTS.md`](AGENTS.md)
- [implementation plan](.omx/plans/rk3588-motion-control-middleware-plan.md)
- [architecture decisions](docs/decisions/)
- [legacy behavioral contract](docs/architecture/LEGACY_BEHAVIOR_BASELINE.md)
- [third-party provenance](third_party/README.md)

## Safety

Do not move hardware, change persistent drive parameters, alter target network
or device-tree configuration, or deploy to production without explicit
authorization and the applicable hardware-test preflight.

