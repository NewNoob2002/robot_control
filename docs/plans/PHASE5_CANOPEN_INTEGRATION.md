# Phase 5 Non-Actuating CANopen Integration Plan

Status: **PLANNED**

Plan date: 2026-08-24

## Objective

Integrate the pinned CANopenLinux/CANopenNode pair as a Linux-native,
single-owner CANopen communications service. Phase 5 observes one externally
configured ZLAC8015D V4 drive at node ID 1 and 500 kbit/s without authorizing
motion. It must preserve raw protocol evidence and publish immutable,
monotonic-timestamped observations for later application and safety adapters.

## Fixed decisions

- ADR-0002 selects CANopenLinux commit
  `f1348d4072cdabea4c3435a13c721ac29ab4cc91` and its CANopenNode submodule
  commit `ef9ac3a2279e34855a20c787fc1bc48bc995ec22`.
- Use CANopenLinux's maintained SocketCAN/epoll driver. Do not create a second
  CANopen transport over `platform/linux/can::CanSocket`. The Phase 4 API
  remains available for diagnostics and non-CANopen users.
- One execution context owns the CANopen stack, object dictionary, socket,
  timers, and protocol state. Upstream callbacks do not run robot policy.
- Normal Phase 5 Debug and Release operation is observation-only and must not
  transmit at startup or periodically. Any NMT or read-only SDO exercise is a
  separately built, explicitly authorized commissioning path.
- Startup configuration is injected and validated. Node IDs, interface name,
  bit rate, and timeouts are not scattered constants.
- Current CANopenNode documentation was checked for the upstream initialization
  and processing model. Each slice must verify the exact API against the pinned
  commits rather than assuming the latest documentation is byte-compatible.
- The legacy STM32 Phase 5 implementation is behavioral evidence only. No RTOS,
  ISR, debugger-request, or BSP code is copied into this Linux repository.

## Boundaries and non-goals

- No CiA402 mode, controlword, fault-reset, quick-stop, or target-velocity write.
- No RPDO production, SDO download, EEPROM/store operation, LSS, SYNC, TIME,
  gateway, trace, ROS2, or motion-control integration.
- No automatic target NMT transition or read-only SDO request. Target CAN
  transmission requires a separate user authorization and test preflight.
- No CAN interface creation, bit-rate configuration, restart configuration,
  link recovery, device-tree change, deployment, or service installation.
- No physical-axis assignment is inferred from packed vendor values. Raw
  low/high halves, bytes, timestamps, and generations remain available.
- Phase 5 communication health is not motion authorization. The control-cycle
  owner and robot-level safety state remain outside this phase.

## Delivery slices

### P5.1 — Immutable dependency pair (complete)

Replace the mixed `components/CANopenNode` snapshot with the exact dependency
pair selected by ADR-0002. Preserve attribution through Git history rather than
copying the old snapshot to another directory. Record repository URLs, commits,
licenses, submodule relationship, and local patch status. Normal configure and
build operations must remain offline.

Acceptance:

- Both checked-out revisions exactly match ADR-0002 and the nested submodule
  relationship.
- No third-party source is edited locally; any required patch is separate,
  minimal, documented, and tested.
- A provenance check fails on a wrong or dirty dependency revision.
- The existing host build remains green before CANopen sources are linked.

Closure: the 2026-08-25 remediation qualification passes dependency, recursive
snapshot, non-SocketCAN host, ShellCheck, and RK3588 Debug/Release cross gates.
Target-runtime HIL is not applicable because P5.1 links no CANopen source. See
`docs/verification/P5_1_DEPENDENCY_QUALIFICATION.md`.

### P5.2 — Minimal stack build and object dictionary (complete)

Add the smallest CMake targets needed for NMT/heartbeat observation, heartbeat
consumer, EMCY consumer, SDO client, and receive PDO processing. Keep the
project warning policy on project-owned code and isolate upstream warning
policy. Check in a fixed, reviewable object dictionary and its generation
provenance; do not add a runtime generator or network fetch.

The initial configuration has one local controller node, one remote heartbeat
consumer for node 1, one SDO client, and four receive PDO slots. Unused
CANopenNode modules remain disabled.

Acceptance:

- Host Debug and Release configurations compile the selected upstream sources.
- RK3588 Debug and Release cross configurations compile and pass the existing
  ELF audits.
- Object-dictionary storage is deterministic and owned only by the CANopen
  execution context.
- Invalid node IDs, interface names, bit rates, and nonpositive timeouts fail
  before socket activation.

Closure: source revision `bd88a72453f7b071b56d0ab628ce9ab5cd95af59`
passes the dependency verifier, Host Debug/Release 18-test non-SocketCAN suites,
exact source/warning audits, and network-disabled RK3588 Debug/Release builds
with ELF audits. No CAN, `vcan`, target, deployment, or motion operation was
executed. See `docs/verification/P5_2_STACK_BUILD_BASELINE.md`.

### P5.3 — Single-owner Linux lifecycle

Create the project-owned CANopen facade and one bounded event loop around the
upstream Linux driver. It opens only an externally configured interface, uses
monotonic elapsed time for processing, consumes SIGINT/SIGTERM through the
existing synchronous termination mechanism, and destroys all upstream objects
and descriptors deterministically.

The owner handles communication reset and endpoint reopen explicitly. It may
restore observation after a link interruption but cannot reauthorize motion or
configure the link.

Acceptance:

- Exactly one context calls CANopenNode initialization and process functions.
- Startup failure reports operation, interface/node identity, upstream error,
  and `errno` where applicable.
- Deadline, signal shutdown, interface loss, and reopen are bounded and leak no
  descriptor or receiver.
- Normal observer mode has no reachable transmit submission.

### P5.4 — Immutable observation contract

Publish one versioned snapshot containing the remote node boot generation, NMT
state, heartbeat observation, EMCY record, SDO result when commissioned, and
four TPDO observations. Each record retains raw COB-ID, DLC, payload, protocol
values, receive timestamp, and observation generation. Optional kernel
metadata remains raw diagnostic data.

Boot-up, heartbeat loss, EMCY, CAN error, malformed frames, interface loss, and
reopen advance or invalidate the appropriate generation. Stale raw values stay
diagnostic and are never presented as fresh feedback. No vendor-specific
physical-axis meaning is added in this slice.

Acceptance:

- Host tests cover exact freshness boundaries, future timestamps, replayed
  generations, malformed DLC, boot/restart invalidation, and mixed-generation
  rejection.
- Readers receive immutable coherent snapshots and never access live upstream
  objects.
- Protocol callbacks perform bounded owner-local updates only; they do not call
  domain safety, arbitration, CiA402, ROS2, or logging policy.

### P5.5 — Managed `vcan` receive evidence

Extend the existing capability-aware namespace runner with a deterministic
CANopen peer. Exercise the production owner path with boot-up, heartbeat/NMT
state, EMCY, SDO response, TPDO1..4, timeout, restart, shutdown, and endpoint
reopen sequences. Unsupported kernel or namespace capability remains an
explicit skip; a configured scenario failure is a test failure.

Acceptance:

- Observer output preserves exact raw identifiers, DLC, payloads, timestamps,
  and generations for every required CANopen service.
- Missing heartbeat and stale TPDOs invalidate current observations at the exact
  configured monotonic boundary.
- An independent socket observes zero frames transmitted by normal observer
  startup and runtime.
- Reopen requires a fresh boot generation before remote state becomes current.

### P5.6 — Debug-only read-only commissioning path

Add one small commissioning executable or build-only mode around the same owner.
A single transmit authorization point at the upstream driver boundary rejects
all traffic by default. The commissioning build may permit only one-shot,
node-1 NMT requests and SDO uploads from a reviewed object whitelist. It has no
startup or periodic trigger, allows at most one bounded retry, and correlates
responses to the live request generation.

The initial upload whitelist is limited to identity and passive diagnostic
objects already recorded in `ZLAC8015D_CANOPEN_NOTES.md`. PDO mapping inventory
or additional vendor objects require a later review; SDO downloads and RPDOs
remain structurally unavailable.

Acceptance:

- Managed `vcan` proves exact NMT and SDO upload frames, response/abort parsing,
  timeout, one retry, late-response rejection, and request-generation replay
  rejection.
- Adjacent indices, unlisted subindices, write commands, RPDO COB-IDs,
  `0x6040`, `0x6060`, `0x60FF` downloads, and broadcast/unscoped requests never
  reach the socket.
- Normal Debug and Release artifacts do not contain the commissioning entry
  point or its transmit authorization.
- No target commissioning command is run without a new explicit authorization
  and hardware-test preflight.

### P5.7 — Cross, target-passive, and closure evidence

Run the complete host suite, managed `vcan` integration, changed-source static
checks, and RK3588 Debug/Release cross builds. On an authorized RK3588 target,
run only the normal observer as an ordinary user against an externally
configured CAN interface. Re-inventory target identity and CAN state at test
time; do not assume the Phase 4 residual state still exists.

Retain raw stdout, independent CAN capture, artifact checksum/ELF audit, exact
commands and exits, bounded shutdown, and postflight receiver/process checks.
Any target NMT or read-only SDO exercise remains a separate active subgate with
its own authorization, allowed frames, deadline, and cleanup plan.

Acceptance:

- Host and managed `vcan` tests pass with no unexpected skip or retry.
- RK3588 Debug and Release artifacts match the reviewed source and sysroot.
- The target observer opens and receives as a non-root user, preserves raw
  CANopen evidence, exits on deadline/SIGTERM, and leaves no receiver/process.
- Independent capture proves the normal observer emitted no CAN frame.
- `docs/verification/PHASE5_CANOPEN_BASELINE.md` records verified facts,
  unavailable hardware checks, residual target state, and prohibited traffic.

## Slice order and review rule

Implement P5.1 through P5.7 in order. Each slice must leave a reviewable commit
with its narrow tests passing; runtime evidence may be a following documentation
commit when it requires a separate privileged or hardware run. Do not start a
later slice to hide an unresolved failure in an earlier one.

## Phase 5 completion gate

Phase 5 is complete only when the pinned upstream pair is reproducible offline,
one owner publishes coherent raw CANopen observations, normal operation has no
transmit path, managed `vcan` covers NMT/heartbeat/EMCY/SDO/PDO/lifecycle
behavior, both RK3588 cross configurations pass, and passive target evidence is
archived. None of those results authorizes CiA402 state changes or motion.
