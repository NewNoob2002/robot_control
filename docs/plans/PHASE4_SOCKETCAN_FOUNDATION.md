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

1. **Frame codec:** typed Classical CAN storage, validation, Linux ABI
   encode/decode, and host unit tests.
2. **Socket lifecycle:** nonblocking socket creation, interface-index lookup,
   bind, filter/error-mask options, deterministic close, and negative tests.
3. **I/O observations:** deadline/cancellation-aware receive, complete-frame
   transmit, timestamps, overflow counters, and raw error-frame preservation.
4. **Tooling:** passive `tools/can_probe` inspection with bounded runtime and
   structured output.
5. **Integration:** bidirectional `vcan` tests, filter tests, interface-down and
   reopen behavior, plus an explicit capability-aware test runner.
6. **Target evidence:** read-only interface/driver inventory and passive capture
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

The `vcan` runner must distinguish unsupported infrastructure from a product
failure. Creating or changing a `vcan` device requires `CAP_NET_ADMIN` and stays
outside the library. Public CI may report an explicit skip until a privileged
integration runner is available.

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

The first delivery slice implements the policy-free Classical CAN frame codec
and its host unit tests. Socket lifecycle, I/O, `vcan`, probe tooling, and target
evidence remain pending in this branch.
