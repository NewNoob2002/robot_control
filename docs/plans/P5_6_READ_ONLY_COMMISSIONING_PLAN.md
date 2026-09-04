# P5.6 Debug-only Read-only Commissioning Plan

Status: **APPROVED FOR MANAGED-VCAN IMPLEMENTATION**

Plan date: 2026-08-26

## Objective

Add one separately enabled Debug-only commissioning artifact around the P5.5
single-owner lifecycle. Normal builds remain receive-only. Commissioning may
send exactly one reviewed node-1 NMT inhibit transition or one whitelisted SDO
upload request at a time; it cannot download an object, produce an RPDO, target
more than node 1, broadcast, reset a node, or authorize motion.

Only namespace-local managed `vcan` traffic is authorized in this slice. No
physical CAN or RK3588 commissioning command is authorized by this plan.

## P5.5 prerequisite review

The P5.5 implementation and evidence were independently re-reviewed before
this plan. No blocking finding remains. Fresh scoped LLVM checks, CANopen/Linux
tests, and the managed-vcan receive scenario passed.

## Pinned and current API check

Current CANopenNode documentation describes nonblocking
`CO_SDOclientUploadInitiate()`, cyclic `CO_SDOclientUpload()`,
`CO_SDOclientUploadBufRead()`, and NMT-master `CO_NMT_sendCommand()`. The pinned
commit uses compatible signatures. Its SDO build has segmented and block
transfer disabled, so this slice accepts expedited uploads of at most four
bytes only. The pinned Linux driver calls `send(fd, can_frame, CAN_MTU,
MSG_DONTWAIT)` and may retry the same buffered frame after `EINTR`, `EAGAIN`, or
`ENOBUFS`; the authorization boundary must therefore permit retries of one
logical frame but consume authorization after exactly one successful kernel
submission.

## Build separation

- Add `ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING`, default `OFF`.
- Enabling it outside a single-config Debug build fails configuration.
- Normal upstream and communication libraries retain the existing unconditional
  deny gate and contain no commissioning authorization symbols.
- The option builds separate commissioning variants plus one
  `robot-control-canopen-commission` executable and commissioning-only tests.
- No normal application or test receives an implicit commissioning entry point.

## Single transmit boundary

The commissioning driver variant replaces the upstream driver's `send` call
with one C boundary. A thread-local single-owner authorization slot is the only
mutable state justified by the pinned driver's context-free send signature.
There is no raw-frame authorization API. Callers may request only:

- NMT Stopped: `0x000#02 01`;
- NMT Pre-operational: `0x000#80 01`; or
- SDO upload: `0x601#40 <index-le> <subindex> 00 00 00 00`.

Operational, reset-node, reset-communication, broadcast node 0, nodes other
than 1, SDO downloads, RPDO identifiers, and every non-whitelisted object are
rejected before the kernel syscall. An unexpected frame clears authorization
and fails closed with `EACCES`. Backpressure retries remain authorized only
until the first complete `CAN_MTU` send succeeds.

## Initial upload whitelist

The two vendor PDFs mark the following exact entries read-only. This first
slice intentionally omits broader mapping inventory, motor-side telemetry, and
every writable object.

| Index:sub | Size | Purpose |
|---|---:|---|
| `0x1000:00` | 4 | Device type |
| `0x1001:00` | 1 | Error register |
| `0x1009:00` | 2 | Hardware version |
| `0x100A:00` | 2 | Software version |
| `0x1018:01` | 4 | Vendor ID |
| `0x1018:02` | 4 | Product code |
| `0x2031:00` | 2 | Vendor software version |
| `0x2032:03` | 2 | Drive temperature |
| `0x2035:00` | 2 | DC bus voltage |
| `0x603F:00` | 4 | Last fault register |
| `0x6041:00` | 4 | Dual-axis statusword |
| `0x6061:00` | 1 | Active mode display |

`0x6040`, `0x6060`, `0x60FF`, `0x2010`, heartbeat configuration, PDO
configuration, and adjacent indices/subindices are not in the whitelist.

## Owner and request lifecycle

- One `CommissioningSession` runs only on the lifecycle owner thread.
- A fresh boot generation and current heartbeat are required before NMT or SDO
  authorization.
- Each user request receives a nonzero monotonically increasing request
  generation; each attempt receives a nonzero attempt generation.
- One SDO request may make one initial attempt and, only when explicitly
  requested, one retry.
- Timeout closes the pinned SDO client, cancels the observation token, and runs
  one timeout-length receive-only quarantine before a retry. Late frames during
  quarantine are retained as rejected raw diagnostics and cannot complete the
  next attempt.
- Accepted upload and abort results must match node, transport, boot, request,
  attempt, index, subindex, command specifier, and expected expedited size.
- Duplicate, unsolicited, late, wrong-index, wrong-subindex, mixed-generation,
  and post-completion responses remain non-current and increment a rejection
  counter.

CANopen expedited SDO has no wire-level transaction nonce. The implementation
therefore does not claim that a response delayed beyond the full timeout plus
quarantine interval can be distinguished from a new identical request. The
tool forbids overlapping requests and bounds the documented guarantee to the
request/attempt windows exercised here.

## CLI boundary

The executable requires one interface and exactly one operation:

```text
robot-control-canopen-commission --interface IFACE --nmt stopped
robot-control-canopen-commission --interface IFACE --nmt pre-operational
robot-control-canopen-commission --interface IFACE --upload INDEX:SUBINDEX [--retry-once]
```

There is no startup action, periodic action, node argument, raw CAN argument,
write/download argument, or deployment default. Invalid or duplicate input
fails before lifecycle creation.

## Test-first sequence

1. Add failing host contracts for whitelist/gate rejection, exact one-shot
   frames, retry-safe authorization, SDO result correlation, and CLI parsing.
2. Add a failing managed-vcan peer covering exact NMT, expedited upload, abort,
   timeout, one retry, late response, duplicate response, and zero prohibited
   traffic.
3. Implement the smallest commissioning-only gate, session, observation
   extension, and executable that satisfy those contracts.
4. Prove normal Debug/Release builds contain neither the commissioning entry
   point nor authorization symbols.
5. Run Debug/Release host regression, commissioning Debug tests, ASan/UBSan,
   scoped LLVM checks, and clean RK3588 Debug/Release normal cross builds.

## Acceptance mapping

- Exact NMT/SDO frames: independent managed-vcan capture.
- Response/abort parsing: upstream result plus project raw observation equality.
- Timeout/one retry: two and only two exact upload requests.
- Late/replay rejection: inactive/quarantine and post-completion duplicate
  responses stay non-current and increment rejection evidence.
- Prohibited traffic: direct gate contracts plus independent socket silence.
- Normal-artifact isolation: default-off build and symbol/target audits.
- Physical safety: no target commissioning run in P5.6.
