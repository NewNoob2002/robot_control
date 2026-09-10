# P6.1 Command-Transcript Acceptance and Inventory Value Review

Review date: 2026-09-04

Status: **COMPLETE**

## First-session RK3588 capture acceptance

The operator explicitly accepts the first authorized session's streamed RK3588
`candump -ta -e can0` command transcript as the independent capture evidence
for manifest positions 1 through 96. The transcript contained 96 ordered
`0x601` requests and their 96 matching `0x581` responses. Those frame bytes
match the archived aggregate in
`evidence/p6_1_20260904/jcan_sdo_reads.json`.

The target packet delta was 193: the accepted 192 frames plus the position-97
`0x2000:00` request whose response timed out. This acceptance closes the
separate-file packaging gap; it does not claim that a missing file was an
original raw artifact or erase the reported timeout. The authorized
continuation retained a separate 46-line RK3588 raw file for positions 97
through 119.

## Value-review method

All 119 successful expedited uploads were checked for request/response
correlation, returned width, little-endian decoding, object/subindex identity,
internal PDO consistency, documented range or enumeration where available,
and preservation of manufacturer contradictions. Version 1.01 was visually
reviewed at the identity, PDO, vendor-parameter, emergency/brake, CiA402 status,
mode, velocity, and fault tables; Version 1.00 was cross-checked for the same
objects.

## Reviewed findings

| Area | Actual fixture values | Review result |
|---|---|---|
| Device/error | `0x1000=0x00020192`, `0x1001=0` | Structurally valid. The device-type value differs from the manuals' `0x00040192`; preserve raw and do not infer the differing high word. |
| Version/identity | `0x1009=1`, `0x100A=1`, `0x2031=0x6381`; `0x1018:01=0x100`, `:02=1` | Vendor/product match the tables. Version encodings are vendor-opaque and remain raw. |
| Identity count | `0x1018:00=5` | Manufacturer contradiction retained. Both manuals define only `00/01/02`; the raw value 5 does not by itself authorize or prove undocumented `:03/04/05`. The documented identity scope is complete. |
| RPDO communication | `0x1400..03` use COB-IDs `0x201/301/401/501`, type `0xFF`, zero inhibit/reserved, event value 1000 | Valid node-1 values and complete subindex coverage. |
| RPDO mapping | `0x1600` count 2 maps `0x6040:00/16` and `0x6060:00/8`; `0x1601` count 1 maps packed `0x60FF:03/32`; `0x1602/03` disabled | Internally consistent. Packed target mapping remains outside the initial Phase 6 command path. |
| TPDO communication | `0x1800..03` use COB-IDs `0x181/281/381/481`, type `0xFF`; TPDO0 event 100, others 0 | Valid node-1 values and complete subindex coverage. |
| TPDO mapping | `0x1A00` count 2 maps `0x6041:00/32` and `0x606C:03/32`; stored third entry `0x6064:00/32` is inactive; `0x1A01..03` disabled | Internally consistent. The inactive third value must be preserved on rollback but is not transmitted while count is 2. |
| Communication/speed limits | `0x2000=0`, `0x2007=0`, `0x2008=200`, `0x200F=1` | All values are documented/range-valid. `0x2000=0` means communication-loss protection is disabled and is a safety prerequisite to resolve before motion authorization. `0x200F=1` selects synchronous control. |
| Fault/status | `0x603F=0`, `0x6041=0x14001400` | No reported fault. Both neutral halves are `0x1400`; retain raw halves. This is a zero/disabled snapshot, not physical-axis mapping evidence. |
| Mode/velocity | `0x6060=3`, `0x6061=3`; `0x606C:00=3`; `:01/02/03=0`; `0x60FF:01/02=0` | Velocity mode request/display match; independent and packed feedback plus both targets are zero. Scale, sign, and physical-axis mapping remain P6.5 work. |
| Quick stop | `0x605A=5` | Documented value/default. Actual stopping behavior is not proved by the read and remains P6.6 work. |
| Emergency inputs | `0x2003=0`, `0x2026:03=0`, `0x2030:01=0`, `:02=9`, `:03=9` | Both X0/X1 are configured as emergency inputs with no inversion and were inactive during capture. Electrical and stop behavior remain P6.6 work. |
| Brake outputs | `0x2030:04=0`, `:07=0`, `:08=0` | Raw values are valid. The manuals contradict each other on applied/released/energized wording, so physical brake semantics remain unresolved and must not be inferred. |

## Acceptance decision

The accepted command transcript plus archived JCAN records are sufficient for
the planned positions 1 through 96. All 119 planned manifest values are
reviewed and every planned later write has an exact original value.

P6.1 is complete. The `0x1018:00` count mismatch remains a documented vendor
contradiction, not evidence that undocumented subindices exist. No request to
`0x1018:03/04/05` is required or authorized by this review.
