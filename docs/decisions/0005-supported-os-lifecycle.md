# ADR-0005: Supported OS Lifecycle

- Status: Accepted
- Date: 2026-07-31

## Context

The current LubanCat image is Debian 11 Bullseye on Linux 5.10.160. Debian 11
LTS ends on 2026-08-31. ROS2 Humble on Debian Bullseye arm64 is a Tier-3,
source-build platform, while Ubuntu 22.04 arm64 is Tier 1.

## Decision

Treat the existing Debian 11 image as a short-term hardware compatibility
baseline, not as the unquestioned long-term production OS.

Phase 1 may target Debian 11 to unblock existing hardware, but before a
production release the project must select and qualify a supported OS path:

1. vendor-supported newer LubanCat image, preferred when available;
2. Ubuntu 22.04 arm64 + ROS2 Humble when board support is adequate;
3. newer Debian plus a separately validated ROS2 source deployment.

The standalone core must remain ROS2-independent so this migration does not
redesign safety/control.

## Alternatives

- Commit indefinitely to Debian 11: rejected due to imminent security support
  end.
- Change OS immediately before hardware probing: rejected because vendor CAN,
  UART, DT overlay, and GPU/board support must first be inventoried.
- Put the whole low-level daemon in a ROS2 container: rejected as the safety
  baseline; a container may host the ROS2 adapter after isolation testing.

## Consequences

- Two temporary build baselines may coexist during migration.
- Kernel/DT/device behavior requires qualification on each candidate image.
- ROS2 distribution selection remains deferred until the OS decision.

## Verification

- Record vendor support, security lifetime, kernel/device availability, ROS2
  support tier, target resource usage, and HIL results for each candidate.
- No production release is approved on an OS without an explicit maintenance
  owner and security-update path.

