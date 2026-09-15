# Delayed read-only CAN request — 2026-09-14

Evidence points to transmission lifetime below the application: a submitted
frame can outlive its sending socket. The exact physical queue location remains
unresolved. No production code, physical CAN configuration, drive state or
hardware trace setting was changed during this investigation. The operator
confirms drive power off, stopped/safe wheels, normal unplug stop, no reconnect
restart and a stationary left wheel.

## Physical evidence

The consumed repair-artifact trial remains FAIL_POST_ERROR_PHYSICAL_REQUEST.
Its one late frame is the read-only upload 601#406C600200000000, 4360.145 ms
after the first controller error. No second nonzero target or CANopenLinux
send/Permission denied diagnostic appeared. All 317 ordinary target frames
match in order within 16914 JCAN frames; three additional target records are
CAN errors.

The wrapper samples counters_executor_exited.json only after child.poll()
reports application exit. At that point the capture contains 10979 bytes,
ending at the first CAN error, and TX packets is 314. The completed capture
contains 19669 bytes and exactly one subsequent host request, the late speed
upload; TX packets is then 315. The additional observed transmission occurs
after application exit. This does not establish a new application submission.
See [exit correlation](evidence/p6_delayed_tx_20260914/exit_correlation.json).

Pinned CANopenLinux CO_CANsend accepts a complete successful send() as success
and retains its own buffer only on EINTR/EAGAIN/ENOBUFS. Its
CO_CANmodule_disable removes epoll registration and closes the socket; it does
not administratively stop the CAN device. The earlier SDO repair cancels its
upstream buffer and invalidates authorization, but cannot retract data already
accepted below that layer.

## Isolated experiments

All host experiments use fresh user/network namespaces with no physical
interface visible and only one read-only-shaped frame per experiment.

| Experiment | Result and limitation |
| --- | --- |
| vcan0 with netem | Immediate local observation; delayed-delivery assertion failed. Not accepted as proof of delayed transmission |
| vxcan pair, local loopback disabled, netem 400 ms | Identical frame received by peer 400.070208 ms after sender socket close. Demonstrates queued delivery beyond socket lifetime |
| Same virtual setup, interface down after socket close | No peer frame within 2 s. Supports cancellation for this host qdisc only |
| RK3588 virtual experiment | Unknown device type at interface creation; no frame submitted. Unavailable, not a target pass |

Host kernel is 7.0.0-31-generic, x86_64. Target kernel is 6.1.84, aarch64,
July 3, 2026 build, with built-in rockchip_canfd. Different kernels and the
absence of real controller hardware limit the host experiment's applicability.
The first unsuccessful vcan attempt and unavailable target experiment are retained.

Scripts and logs are in [delayed-TX evidence](evidence/p6_delayed_tx_20260914/).
The successful host setup creates vxcan0/vxcan1 inside an unshare user/network
namespace and attaches a netem qdisc with 400 ms delay to vxcan0. The script
asserts those are the only interfaces besides loopback. Its --link-down variant
changes only the virtual sender and submits no additional frame.

## Remaining work

The failed physical trial has no send-syscall trace, TX-completion trace or
controller register snapshot. Qdisc backlog, a driver-owned buffer and a
controller mailbox/retransmission slot therefore remain distinguishable
hypotheses; the exact enqueue time and queue location were not measured.

The next repair requires source matching the actual kernel build to inspect
transmit, completion and stop/abort paths. Interface-down inhibition is a
candidate, but real controller cancellation must be verified before becoming
a safety claim. It belongs behind an explicit privileged platform boundary,
not in protocol/domain code or an unconditionally privileged motion application.
Reopening communication must not reauthorize motion.

No automatic retransmission/restart setting was changed. Extra zero writes or
retries would add traffic while lower-layer state is uncertain and are not a
substitute for cancellation. Preserve the strict failed result and consumed
markers; keep the drive powered off. The application/socket investigation is
complete; exact driver-level cause and physical repair remain open.

The checked target headers directory does not contain rockchip_canfd.c; the modules source symlink is absent. The build symlink points only to linux-headers-6.1.84. This check does not identify a matching source commit. See target_source_availability.log.
