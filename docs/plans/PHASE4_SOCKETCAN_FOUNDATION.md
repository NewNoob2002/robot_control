# Phase 4 SocketCAN Foundation Plan

## Objective

Establish policy-free Linux SocketCAN mechanics before integrating CANopen.
Phase 4 owns Classical CAN frame validation, socket/interface binding, kernel
filtering, nonblocking I/O, timestamps, error/link observations, `vcan`
integration evidence, and passive target verification.

## Boundaries and non-goals

- `platform/linux/can` may wrap Linux CAN socket ABI and syscalls only.
- It must not contain CANopen, CiA402, node-ID, motion, arbitration, recovery, or
  safety policy.
- The library never creates, configures, or brings up a CAN interface.
- `tools/can_probe` is passive by default and must require an explicit option
  before any test-frame transmission.
- Phase 4 does not deploy to the target, alter target networking, move hardware,
  or change persistent drive parameters.

## API and error contract

- Preserve the raw Linux CAN identifier and error-class bits in observations.
- Reject invalid standard identifiers, payload lengths, DLC combinations, and
  transmit attempts carrying `CAN_ERR_FLAG` before issuing a syscall.
- Open sockets with `SOCK_NONBLOCK | SOCK_CLOEXEC`; bind by stable interface
  name and report the operation, interface, and captured `errno`.
- Treat short Classic CAN reads/writes as protocol I/O failures; never expose a
  partially initialized frame.
- Filters are injected at open/configuration time and retain their raw mask
  values. Empty-filter behavior must be explicit rather than kernel-default
  dependent.
- Receive operations use monotonic deadlines and the existing cancellation-fd
  convention. Timeout returns no frame; cancellation returns `ECANCELED`.
- Kernel timestamps, RX queue overflow counters, and error frames remain raw
  diagnostic data. Higher layers may decode them without erasing the source
  values.

## Delivery sequence

1. **Frame codec (complete):** typed Classical CAN storage, validation, Linux ABI
   encode/decode, and host unit tests.
2. **Socket lifecycle (complete):** nonblocking socket creation, interface-index
   lookup, bind, filter/error-mask options, deterministic close, and negative
   tests.
3. **Basic frame I/O (complete):** nonblocking complete-frame send/receive, one
   monotonic deadline per operation, cancellation-fd observation, timeout and
   retry handling, and raw error-frame preservation on receive.
4. **I/O metadata (complete):** optional raw kernel nanosecond timestamps and RX
   queue overflow counters from receive ancillary data.
5. **Tooling (complete):** passive-only `tools/can_probe` inspection with a
   bounded monotonic runtime, signal cancellation, and structured frame and
   metadata output.
6. **Integration (in progress):** a capability-aware isolated runner and real
   bidirectional `vcan` frame and raw-filter-isolation tests are complete;
   error-frame runtime evidence plus interface-down and reopen behavior remain.
7. **Target evidence:** read-only interface/driver inventory and passive capture
   on the authorized RK3588 target; record kernel limitations without changing
   network configuration.

## Verification matrix

| Layer | Required evidence |
| --- | --- |
| Host unit | ID/DLC/flag boundaries, encode/decode, error-frame preservation |
| Host integration | bind errors, filters, timeout, cancellation, short/error I/O |
| `vcan` | bidirectional frames, filter isolation, overflow/error observations, reopen |
| Cross build | Debug and Release builds plus ELF audit against reviewed RK3588 sysroot |
| Target passive | interface identity, receive-only capture, timestamps/counters, no root data path |
| Static | warnings-as-errors, ShellCheck, Hadolint, changed-source format/static analysis |

The managed `vcan` runner distinguishes unsupported infrastructure from a
product failure. It creates `vcan0` only inside an isolated user/network
namespace, reports capability or kernel limitations as a CTest skip, and
propagates test failures after setup.

## Acceptance criteria

- All malformed frames and syscall failures carry operation and interface/frame
  context.
- Host and `vcan` tests cover the documented negative paths deterministically.
- Normal SocketCAN data access requires no process privilege once an interface
  has been configured externally.
- Passive target capture observes expected traffic and preserves raw diagnostic
  metadata.
- No Phase 4 component can authorize motion or configure the CAN link.

## Current implementation checkpoint

The completed delivery slices provide the policy-free Classical CAN frame
codec, move-only SocketCAN socket lifecycle, and basic complete-frame
send/receive with monotonic timeout and borrowed cancellation-fd semantics.
Receive-all, receive-none, raw-filter, and error-mask configuration are
explicit. Receive observations preserve optional raw realtime-domain kernel
timestamps and cumulative RX queue overflow counters for diagnostics; they do
not use either value for monotonic deadlines or safety decisions. The
`robot-control-can-probe` tool is receive-only, externally configured,
steady-clock bounded, and emits stable key/value records without interpreting
diagnostic metadata. The managed isolated `vcan` runner has produced runtime
evidence for bidirectional complete Classical CAN frames and exact raw-filter
isolation; unsupported namespace or kernel capability is an explicit CTest
skip. Nonzero overflow and error-frame runtime evidence, interface reopen
behavior, and passive target validation remain pending.
