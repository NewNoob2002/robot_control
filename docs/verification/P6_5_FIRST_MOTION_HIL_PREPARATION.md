# P6.5 One-axis First-motion HIL Preparation

Prepared: 2026-09-07T10:55:16Z. Updated: 2026-09-08.
Status: **P6.5 COMPLETE — BOTH INDEPENDENT CHANNELS MAPPED WITH MATCHING RK3588/JCAN EVIDENCE**.

## Reviewed software scope

The Debug-only, default-OFF qualification artifact now executes one complete
first-motion sequence from the P6.4 safe terminal state. It verifies the
read-only zero baseline, requires volatile `0x200F:00 = 1`, selects asynchronous
command application with verified `0x200F:00 = 0`, enters zero-target Operation
Enabled, executes one bounded independent target, verifies zero at expiry,
returns both CiA402 halves to Shutdown and NMT Pre-operational, then restores
and uploads `0x200F:00 = 1`. It never writes `0x2010`.

The nonzero target download and readback share the same monotonic deadline. A
stalled readback therefore cannot consume a second full SDO timeout before the
zero request. A managed-vcan regression delays that readback and verifies the
zero request still follows within the bounded test tolerance. Both status halves
must be Operation Enabled before the nonzero request, and fresh TPDO1 feedback
must keep the uncommanded protocol half at zero throughout the interval.

The first physical trial showed that one actual-velocity sample immediately
after Shutdown is too early for normal deceleration. Cleanup now uploads
`0x606C:01/02/03` every 50 ms until all three are zero or the existing
transition deadline expires. Every upload shares that one monotonic deadline;
the loop cannot extend it. Signal, lifecycle, SDO, freshness, and CAN failures
still stop the wait, and NMT Pre-operational plus `0x200F:00 = 1` restoration
are still attempted. A timeout remains a failed cleanup.

For the reviewed 2000 ms transition timeout, the executor budget is 13 SDO
downloads, 27 fixed SDO uploads, at most 120 bounded settle uploads, and two
node-specific NMT requests. There is exactly one nonzero download. No motion
retry, target renewal, RPDO, packed-target write, fault reset, node reset,
broadcast NMT, brake-output write, EEPROM save, or persistent configuration is
available.

## Software evidence

Evidence directory: `evidence/p6_5_20260907_software/`.

| Check | Result |
|---|---|
| GCC 13.3 Debug full CTest | 39/39 PASS |
| LLVM 22.1.8 ASan/UBSan full CTest | 39/39 PASS |
| Qualification gate and isolated managed-vcan | PASS |
| Deadline-stalled nonzero readback regression | PASS |
| clang-format warnings-as-errors | PASS |
| clang-tidy analyzer/bugprone scope | Four reviewed existing warnings; no new defect |
| Default Debug/Release and P5.6 artifact isolation | PASS |
| Locked, no-network RK3588 Debug qualification build | PASS |
| AArch64 ELF ABI, dependency, symbol-version and RPATH audit | PASS |

Settle-fix evidence directory: `evidence/p6_5_20260908_settle_fix/`.

| Settle-fix check | Result |
|---|---|
| GCC 13.3 Debug full CTest | 39/39 PASS |
| LLVM 22.1.8 ASan/UBSan full CTest | 39/39 PASS |
| Delayed `0x606C:01=16` then all-zero managed-vcan regression | PASS |
| clang-format warnings-as-errors | PASS |
| clang-tidy analyzer/bugprone scope | Four reviewed existing warnings; no new defect |
| Default Debug/Release and P5.6 artifact isolation | PASS |
| Locked, no-network RK3588 Debug qualification build | PASS, 69/69 |
| AArch64 interpreter, dependency, symbol-version, qualification-symbol and RPATH audit | PASS |

Three-second observation evidence directory:
`evidence/p6_5_20260908_three_second_observation/`.

| Three-second check | Result |
|---|---|
| GCC 13.3 Debug full CTest | 40/40 PASS |
| LLVM 22.1.8 ASan/UBSan full CTest | 40/40 PASS |
| CLI and domain rejection of 3001 ms before transmission | PASS |
| clang-format warnings-as-errors | PASS |
| clang-tidy analyzer/bugprone scope | Four reviewed existing warnings; no new defect |
| Default Debug/Release and P5.6 artifact isolation | PASS |
| Locked, no-network RK3588 Debug qualification build | PASS, 69/69 |
| AArch64 interpreter, dependency, symbol-version, qualification-symbol and RPATH audit | PASS |

Source HEAD is `1d1314d25269d38f9679cd2c15e23df65afcd2a4`; the
reviewed dirty-source snapshot SHA-256 is
`2230f3989c6e2024ad1bbcf0cbfb037cd2b2cf103bb8ebc9dbff1ba5cdaa4109`.
The staged AArch64 ELF SHA-256 is
`52c98513ed0818d9ddddc0cfec6348b9e899822ccd8e6e9f9a827f6f56c328c2`
under `out/staging/p65-52c98513ed08/`. That first-trial artifact remains
preserved and must not be reused.

The settle-fix reviewed dirty-source snapshot SHA-256 is
`99399610538facc1a3358308b76046117152a0a04eb198ac28dba7a6685bfc17`.
The replacement AArch64 ELF SHA-256 is
`2f5d9ce170953a141cb0fd219d83c1a6c087e6e88e89efd187b94119ef2a44fb`
under `out/staging/p65-2f5d9ce17095/`. It was deployed for the completed retry
and remains preserved with that trial evidence.

The three-second reviewed dirty-source snapshot SHA-256 is
`a360ab8a7498476c95e3f0385392a4ce8ee6feb18496bc16397e0b4cb917be33`.
The reviewed AArch64 ELF SHA-256 is
`899914fdccb5df378eabcf4aafab93d23d48cf01ef35098847b8af81d38bc881`
under `out/staging/p65-observe-899914fdccb5/`. It was deployed for the
completed three-second trial and matched on the RK3588.

## First authorized trial result

Evidence directory: `evidence/p6_5_20260908_first_motion_1/`.

The one permitted `0x60FF:01 = +5 rpm` request was sent at
`1788834424.306990`; the first zero request was sent at
`1788834424.807989`, **500.999 ms** later and inside the 520 ms limit. The
request, acknowledgement, readback, zero, Shutdown, NMT Pre-operational, and
`0x200F=1` restoration are present in the RK3588 capture. JCAN captured 124
frames with zero drops, exactly matching the first 124 RK3588 frames. No SDO
abort, EMCY, or CAN error frame was observed.

The attempt remains **FAIL** because `0x606C:01` returned raw `16` (1.6 rpm)
44.312 ms after the first zero request. TPDO1 still reported both packed
velocity halves as zero, so that feedback contradiction is retained. Final
uploads verified heartbeat zero, `0x200F=1`, controlword `0x0006`, dual status
`0x1421/0x1421`, mode 3, zero targets, all independent/packed velocities zero,
and fault zero.

Physical evidence establishes `0x60FF:01 -> left wheel`; a positive target
rotates that wheel counterclockwise when viewed from the chassis left side.
The right wheel did not move. The mechanical brake produced no abnormal action
or sound. The operator powered the drive off after cleanup.

## Authorized retry result

Evidence directory: `evidence/p6_5_20260908_first_motion_retry_1/`.

The settle-fixed retry completed with `qualification_complete node=1
operation=5`. Its sole `0x60FF:01 = +5 rpm` request was sent at
`1788838700.415278`; the first zero request followed at
`1788838700.916263`, **500.985 ms** later. After that zero request,
`0x606C:01` reported 3.2 rpm at 44.325 ms, -0.5 rpm at 97.328 ms, and zero at
150.328 ms. Both other velocity objects were then zero. NMT Pre-operational
followed at 155.888 ms and verified `0x200F=1` restoration at 357.993 ms.

The RK3588 and JCAN captures contain the same 275 frames in the same order;
JCAN reported zero drops and no warnings. No SDO abort, EMCY, CAN error, or
error-counter growth occurred. Final uploads verified `0x1017=0`, `0x200F=1`,
controlword `0x0006`, dual status `0x1421/0x1421`, mode 3, zero targets, all
velocity views zero, and fault zero. No executor, capture process, or JCAN
periodic task remained.

The operator again observed the left wheel rotating counterclockwise from the
chassis left side, the right wheel remaining stationary, and no abnormal
mechanical-brake action or sound. The retry is **PASS**. TPDO1 packed velocity
remained zero while `0x606C:01` measured deceleration, so that feedback
contradiction remains open.

## Three-second observation trial result

The authorized trial used exactly this scope:

- target: ZLAC8015D V4, node 1, existing Classical CAN 500000 bit/s fixture;
- operation: `--target-once 1:5 --duration-ms 3000`;
- nonzero scope: independent target `0x60FF:01 = +5 rpm` once;
- other target: `0x60FF:02 = 0` throughout;
- temporary values: heartbeat `0x1017:00 0 -> 1000 -> 0` and command
  application `0x200F:00 1 -> 0 -> 1`, all volatile and read back;
- target-frame-to-zero-frame limit: at most 3020 ms in the RK3588 raw capture;
- startup: 180-second real-boot operator window; SDO 500 ms; state/NMT 2000 ms;
  heartbeat freshness 1000 ms; TPDO freshness 100 ms;
- observers: one bounded RK3588 raw/error capture and one bounded passive JCAN
  capture, both active before boot; no other CAN requester during the core run.

Evidence directory: `evidence/p6_5_20260908_three_second_trial_1/`.

The sole nonzero request was sent at `1788841503.759221`; the first zero
request followed at `1788841506.760215`, **3000.994 ms** later and inside the
3020 ms limit. After zero, `0x606C:01` reported 1.2, -1.8, 0.4, 0.2, then
0 rpm, reaching zero at 254.256 ms. NMT Pre-operational followed at 259.878 ms,
`0x200F=1` restoration at 345.934 ms, and heartbeat restoration at 353.809 ms.

The RK3588 and JCAN captures contain the same 191 frames in the same order;
JCAN reported zero drops and no warnings. No SDO abort, EMCY, CAN error, or
error-counter growth occurred. Final uploads verified `0x1017=0`, `0x200F=1`,
controlword `0x0006`, dual status `0x1421/0x1421`, mode 3, zero targets, all
velocity views zero, and fault zero. No executor, capture process, or JCAN
periodic task remained.

From the chassis left side, the operator observed the left wheel rotating
counterclockwise throughout the longer interval. The right wheel remained
stationary, and the mechanical brake produced no abnormal action or sound. The
trial is **PASS**.

The operator powered on immediately after the target wrapper printed
`READY_TO_POWER_ON`; that marker means the executor, RK3588 capture, boot helper,
and JCAN capture were already ready. No further chat response was needed before
power-on, avoiding the earlier tool-roundtrip timeout.

The reviewed boot helper waited for the real standard `0x701 / 00` frame,
checked that the executor was still alive, and sent the single authorized
heartbeat configuration. The executor started only after the following current
heartbeat. After the core run, the heartbeat was restored to zero and all final
values were uploaded.

## Exact active frames

| Purpose | CAN ID | DLC | Payload |
|---|---:|---:|---|
| Heartbeat 1000 ms | `0x601` | 8 | `2B 17 10 00 E8 03 00 00` |
| Asynchronous command application | `0x601` | 8 | `2B 0F 20 00 00 00 00 00` |
| Target subindex 1, +5 rpm | `0x601` | 8 | `23 FF 60 01 05 00 00 00` |
| Target subindex 1, zero | `0x601` | 8 | `23 FF 60 01 00 00 00 00` |
| Target subindex 2, zero | `0x601` | 8 | `23 FF 60 02 00 00 00 00` |
| NMT Operational, node 1 | `0x000` | 2 | `01 01` |
| Velocity mode 3 | `0x601` | 8 | `2F 60 60 00 03 00 00 00` |
| Shutdown | `0x601` | 8 | `2B 40 60 00 06 00 00 00` |
| Switch On | `0x601` | 8 | `2B 40 60 00 07 00 00 00` |
| Enable Operation | `0x601` | 8 | `2B 40 60 00 0F 00 00 00` |
| NMT Pre-operational, node 1 | `0x000` | 2 | `80 01` |
| Restore synchronous application | `0x601` | 8 | `2B 0F 20 00 01 00 00 00` |
| Restore heartbeat zero | `0x601` | 8 | `2B 17 10 00 00 00 00 00` |

Each expedited download must receive `0x581 / 60 <index-low> <index-high>
<subindex> 00 00 00 00`. Every upload uses `0x601 / 40 ...` and must return the
requested object, exact width and expected value. Abort, wrong object, mismatch,
timeout, EMCY, CAN error, generation change, stale heartbeat/TPDO, unexpected
traffic, second-wheel motion, or cleanup error stops the trial without retry.

## Physical and acceptance gates

Before deployment or transmission, reverify RK3588 identity, JCAN serial
`207F346D5650`, adapter configuration, `can0` ERROR-ACTIVE state and zero error
growth. The operator must confirm both wheels are raised and unloaded, wiring is
unchanged, the mechanical brake is released and will not resist the commanded
wheel, the area is clear, and an immediately accessible independent power cut is
available. An engaged mechanical brake fails readiness because it adds load and
can hide the mapping result.

Pass requires exactly one wheel to move only inside the bounded interval, the
other wheel and uncommanded feedback half to remain zero, raw sign/direction and
scale evidence to be retained without assigning names prematurely, and final
readbacks of `0x1017=0`, `0x200F=1`, `0x6040=0x0006`, dual
`0x6041=0x1421/0x1421`, mode 3, zero targets, zero independent/packed velocity,
zero fault, and NMT Pre-operational. If verified CAN cleanup cannot complete, the
operator must remove drive power immediately.

The completed trials establish `0x60FF:01 -> left wheel` and positive target as
counterclockwise from the chassis left side, plus `0x60FF:02 -> right wheel`
and positive target as counter-clockwise from the chassis right side. Each independent
three-second trial moved only the selected unloaded wheel. Reverse direction
remains outside P6.5 scope.

## Second-axis preparation and HIL result

Evidence directory: `evidence/p6_5_20260908_second_axis_preparation/`.

The existing first-motion managed-vcan regression now runs the same complete
sequence for both independent channels. The channel-2 case proves the exact
`0x60FF:02=+5` write and readback, low packed-velocity half zero supervision,
expiry zero, `0x606C:02` settle polling, safe-state cleanup, and `0x200F`
restoration. GCC and ASan/UBSan full suites both pass 40/40. Production sources
are byte-identical to the reviewed three-second build, whose staged AArch64 ELF
remains SHA-256
`899914fdccb5df378eabcf4aafab93d23d48cf01ef35098847b8af81d38bc881`.

The authorized physical trial used node 1, `0x60FF:02=+5 rpm` once for
3000 ms, with `0x60FF:01=0` throughout. The first acquisition retained a valid
RK3588 run but JCAN began after the motion sequence, so it was rejected as an
independent-capture failure. The user explicitly requested one reacquisition
under the same bounds.

The reacquisition evidence is in
`evidence/p6_5_20260908_second_axis_trial_2/`. RK3588 and JCAN each captured 215
frames in exactly the same order, with zero JCAN drops or warnings. The sole
nonzero request was `0x60FF:02=+5 rpm`; the first zero followed 3001.004 ms
later, inside the 3020 ms limit. `0x606C:02` reached zero 427.313 ms after the
zero request. No subindex-1 nonzero write, SDO abort, EMCY, CAN error, error
growth, or cleanup residue occurred.

Final uploads verified `0x1017=0`, `0x200F=1`, controlword `0x0006`, dual
status `0x1421/0x1421`, mode 3, both targets zero, all three velocity views
zero, and fault zero. From the chassis right side, the operator observed the
right wheel rotating counter-clockwise for approximately three seconds, the left wheel
remaining stationary, and no abnormal mechanical-brake action or sound. The
trial is **PASS**, and P6.5 is complete. TPDO1 packed velocity remained zero
while `0x606C:02` and physical observation showed motion, so that contradiction
remains explicit for later work.
