# P5.4 Immutable Observation Development Pre-Review

Date: 2026-08-25

Status: **IMPLEMENTED; LIVE SOCKETCAN EVIDENCE DEFERRED TO P5.5**

## Scope

P5.4 adds a project-owned, immutable observation snapshot to the P5.3
single-owner lifecycle. It observes the configured remote node without adding
transmit authorization, another CAN socket, vendor axis meaning, CiA402 policy,
ROS2 integration, or safety decisions.

P5.3 independent review found no blocking defect. P5.4 may build on revision
`8bef091c1d21989ea746c41c24b033e2e4518d01`; its open-endpoint behavior remains
runtime-qualified later by the managed-vcan P5.5 slice.

Implementation revision `05a948660596ecab3ec2f4bf487976b0cb60865b` adds
the test-first observation state machine, copied snapshot API, same-socket raw
peek/consume path, project-owned CAN error-frame receive filter, and lifecycle
generation invalidation. Host Debug/Release, scoped LLVM, sanitizer, and clean
RK3588 Debug/Release cross gates pass. See
`docs/verification/P5_4_OBSERVATION_BASELINE.md`.

## Pinned-driver findings

- `CO_CANrxFromEpoll()` invokes a matched protocol callback before returning
  the copied `CO_CANrxMsg_t` and receive-array index. Unmatched frames are not
  copied. CAN error frames are consumed without a raw return when
  `CO_DRIVER_ERROR_REPORTING` is zero.
- The receive-array timestamp comes from `SO_TIMESTAMPING` system time. It is
  diagnostic only and cannot drive freshness; P5.4 uses the owner
  `steady_clock` timestamp.
- `CO_epoll_processRT()` currently calls `CO_CANrxFromEpoll()` with null output
  pointers. P5.4 must consume the CAN event first, clear `epoll_new`, then call
  `CO_epoll_processRT()` only for RPDO processing.
- The heartbeat receive callback checks `DLC == 1`; heartbeat state and timeout
  callbacks run later in `CO_process()`. The EMCY callback has no object/context
  argument and reads an eight-byte payload without first checking DLC. P5.4
  therefore does not register project heartbeat, EMCY, or RPDO callbacks.
- Current local RPDO mappings contain zero application objects. The verified
  passive drive inventory is raw TPDO1 DLC 8 and TPDO2..4 DLC 0. P5.4 retains
  these as injected expected lengths and does not infer field or axis meaning.

## Minimum design

Add one small observation module used only by the lifecycle owner:

- `RawCanopenFrame`: standard COB-ID, DLC, all eight payload bytes, owner
  monotonic receive time, and observation generation. Optional kernel
  timestamp/drop metadata remains absent unless already available without a new
  syscall or transport.
- `ObservationGeneration`: transport generation plus remote boot generation.
  A record is current only when both values equal the enclosing snapshot.
- `ObservationSnapshot`: monotonically increasing snapshot version, generation,
  boot observation, NMT/heartbeat state, last EMCY, optional commissioned SDO
  result, four TPDO records, and raw malformed/CAN-error diagnostics.
- One owner-local mutable accumulator and one mutex-protected copied snapshot.
  Readers receive a value copy and never retain a pointer to upstream objects or
  owner-local state. Avoid a callback hierarchy, background publisher, queue, or
  per-frame heap allocation.

Extend startup configuration only with the values required for deterministic
validation: one TPDO freshness timeout and four expected TPDO DLC values. The
values are injected and validated before socket activation.

## Receive path

For an `EPOLLIN` event on the sole CAN interface:

1. Capture `steady_clock::now()` once.
2. `recv(..., MSG_PEEK | MSG_DONTWAIT)` exactly one `can_frame` from the existing
   upstream descriptor. This observes error and unmatched frames without
   consuming them and does not create a second transport.
3. Call `CO_CANrxFromEpoll(..., &driver_frame, &message_index)` once so the
   pinned stack remains the sole consumer and performs its normal callbacks.
4. Require matched data frames to equal the peeked frame. Treat a missing
   receive-array index for a non-error data frame as a contextual receive
   failure, not as fresh observation.
5. Clear `epoll_new`, classify the peeked raw frame with the captured monotonic
   time, and then call `CO_epoll_processRT()` for RPDO processing only.
6. Run mainline processing and publish at most one coherent snapshot for the
   completed event-loop iteration.

`MSG_PEEK` behavior and raw-error capture must be proved through the same
managed namespace fixture in P5.5 before target claims. P5.4 host tests exercise
the classifier and generation state without opening a CAN socket.

## Generation and invalidation contract

| Event | Generation action | Current observation action |
|---|---|---|
| Initial open | Increment transport; boot is unobserved | Invalidate all remote records |
| Valid boot-up (`DLC 1`, byte 0 `0x00`) | Increment boot within current transport | Record boot/NMT initializing; invalidate heartbeat, EMCY, SDO, TPDO1..4 |
| Valid heartbeat | Keep generation | Update NMT/heartbeat only after a boot in the current transport |
| Heartbeat timeout | Keep generation | Preserve raw history; invalidate NMT/heartbeat and TPDO freshness |
| Valid EMCY (`DLC 8`) | Keep generation | Record raw and decoded protocol values; do not infer safety action |
| Valid TPDO | Keep generation | Refresh only the addressed TPDO when COB-ID and configured DLC match |
| Malformed known frame | Keep generation | Preserve malformed raw data and invalidate only the addressed record |
| CAN error, interface loss, communication reset, or reopen | Increment transport | Invalidate every remote record and require a new boot-up |
| Old/replayed generation | No change | Preserve diagnostic count; never replace current data |

A timestamp is fresh only when it is not later than the supplied monotonic
`now`, its generation matches, and it has not exceeded the configured inclusive
deadline. An arriving frame at the exact deadline is handled by one documented
comparison used by every record type.

The SDO field exists in the snapshot but remains empty in normal P5.4 builds.
P5.6 will populate it only from an explicitly authorized request generation;
P5.4 does not add an SDO request path.

## Test-first implementation order

1. Add pure host tests for initial invalid state, boot generation, valid NMT
   values, malformed DLC, exact deadline, future timestamps, heartbeat timeout,
   replayed transport/boot generations, mixed-generation rejection, and
   reset/reopen invalidation.
2. Add TPDO tests for the injected four COB-IDs/DLC values and prove that one
   TPDO cannot refresh another. Add EMCY raw/decoded and malformed tests.
3. Implement the smallest accumulator and copied snapshot that satisfies those
   tests.
4. Integrate the raw peek/one-consume receive path into `Lifecycle`, preserving
   the P5.3 default-deny transmit gate and single-owner rule.
5. Re-run fresh Host Debug/Release zero-CAN suites, scoped LLVM checks, driver
   symbol audit, and clean RK3588 Debug/Release cross builds.
6. Defer live boot/heartbeat/EMCY/TPDO/error/reopen sequences and independent
   zero-transmit observation to P5.5.

## Pre-review decision

```yaml
review_result:
  schema_version: 1
  phase: P5.4-development-pre-review
  base_revision: 8bef091c1d21989ea746c41c24b033e2e4518d01
  decision: approved-to-implement
  blockers: []
  required_contracts:
    - same-socket raw peek followed by exactly one upstream consume
    - owner monotonic time is the sole freshness clock
    - malformed frames never refresh observations
    - reset and reopen require a new boot generation
    - readers receive coherent value snapshots
  deferred_to_p5_5:
    - MSG_PEEK SocketCAN runtime proof
    - CAN error-frame raw capture
    - open-endpoint signal, timeout, loss, and reopen evidence
    - independent zero-transmit bus observation
```
