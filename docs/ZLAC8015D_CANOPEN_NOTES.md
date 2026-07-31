# ZLAC8015D V4 CANopen Configuration and Integration Notes

## 1. Sources and Status

This note extracts implementation-relevant information from the following manufacturer documents:

- `ZLAC8015D V4 series CANopen Quick Start Guide`, Version 1.00, 28 PDF pages.
- `ZLAC8015D V4 series CANopen Communication Examples`, Version 1.01, 38 PDF pages.

The original PDFs are the authoritative source. This note is an engineering index and discrepancy log, not a replacement for the manuals. Page references use the PDF page number; the printed page number is normally one less.

Several contradictions exist between, and sometimes within, the documents. Entries marked **VERIFY ON HARDWARE** must not be encoded as safety assumptions until confirmed using SDO reads, a CAN analyzer, and an unloaded drive.

## 2. Device and Network Baseline

| Item | Manufacturer information | Project consequence |
|---|---|---|
| Physical/data link | CAN 2.0A, standard 11-bit identifiers | Use Classic CAN standard frames only |
| CANopen application | DS301 V4.02 and DS402 V2.01 | CANopenNode v4 can provide the controller-side stack, with a custom STM32 port |
| Device role | NMT slave, SDO server, heartbeat producer | STM32 application must act as NMT manager/controller and SDO client |
| PDO capacity | Up to 4 TPDO and 4 RPDO | Reserve only the mappings needed for deterministic motion and feedback |
| Node ID | 1-127, factory default 1 | Assign and document a unique node ID per physical drive |
| Bit rate | 1000/500/250/125/100 kbit/s, factory default 500 kbit/s | Current STM32 FDCAN setting of 500 kbit/s matches the factory default |
| Dual-axis layout | One CANopen node controls left and right motors | One NMT/CiA402 communication endpoint contains two axis targets and feedback values |

Node ID is configured through `0x200A:00`:

- Range: 1-127.
- Default: 1.

Bit rate is configured through `0x200B:00`:

- `0`: 1000 kbit/s
- `1`: 500 kbit/s
- `2`: 250 kbit/s
- `3`: 125 kbit/s
- `4`: 100 kbit/s
- Default: `1`, or 500 kbit/s.

The documents do not clearly state when changed node-ID/bit-rate settings take effect. **VERIFY ON HARDWARE** whether EEPROM save plus communication reset, node reset, or power cycle is required.

## 3. CANopen Identifiers and Byte Order

For node ID `N`:

| Function | COB-ID |
|---|---:|
| NMT | `0x000` |
| SYNC | `0x080` |
| EMCY | `0x080 + N` |
| TPDO0..3 | `0x180/0x280/0x380/0x480 + N` |
| RPDO0..3 | `0x200/0x300/0x400/0x500 + N` |
| SDO server response | `0x580 + N` |
| SDO client request | `0x600 + N` |
| Heartbeat/boot-up | `0x700 + N` |

All multi-byte index and data fields use little-endian byte order.

Expedited SDO command specifiers documented by the manufacturer:

| Command | Meaning |
|---:|---|
| `0x2F` | Download 1 byte |
| `0x2B` | Download 2 bytes |
| `0x27` | Download 3 bytes |
| `0x23` | Download 4 bytes |
| `0x60` | Successful download response |
| `0x40` | Upload request |
| `0x4F/0x4B/0x47/0x43` | Upload response with 1/2/3/4 bytes |
| `0x80` | SDO abort response |

The Version 1.01 RPDO mapping example on PDF page 9 appears to repeat request command bytes in the response column. This conflicts with the earlier SDO definition and the Version 1.00 examples. Treat those response bytes as a documentation error; validate normal `0x60` download responses and report any `0x80` abort.

## 4. NMT and Heartbeat

NMT commands on COB-ID `0x000`:

| Byte 0 | Action |
|---:|---|
| `0x01` | Start / enter Operational |
| `0x02` | Stop |
| `0x80` | Enter Pre-operational |
| `0x81` | Reset node application |

Byte 1 is the target node ID; zero broadcasts to every node. PDO traffic is active only in NMT Operational.

Heartbeat/boot-up arrives on `0x700 + N`:

| Byte 0 | State |
|---:|---|
| `0x00` | Boot-up |
| `0x04` | Stopped |
| `0x05` | Operational |
| `0x7F` | Pre-operational |

Heartbeat producer time is `0x1017:00`. On the identified 2026-07-22
ZLAC8015D V4 fixture, a volatile raw value of 1000 produced `0x701` frames at
approximately 498-501 ms intervals. An upload returned the same raw value.
This confirms 0.5 ms/count for that fixture and resolves the unit contradiction
for the current hardware baseline. The value was restored to zero and was not
saved to EEPROM.

The document recommends the consumer timeout be at least twice the producer interval. The firmware should use a larger reviewed margin to tolerate scheduling and bus jitter, but never allow an indefinite timeout.

## 5. Communication-Loss Protection and Persistence

`0x2000:00` configures the drive's host-communication loss protection:

- Unit: ms.
- Range: 0-32000.
- Manufacturer recommendation: 200-1000 ms.
- Default: 0, which leaves protection disabled.
- Described behavior: if no host instruction is received during the interval, the drive stops the motors.

This protection should be enabled as independent defense in depth, in addition to STM32 source timeouts, CANopen heartbeat monitoring, TPDO freshness checks, and watchdog supervision.

**VERIFY ON HARDWARE** which received frames reset this timer: any CAN frame, NMT, SDO, RPDO, or only motion instructions. Confirm the final stop action and whether operation must be re-enabled afterward.

`0x2010:00 = 1` saves RW parameters to EEPROM. The examples save after PDO configuration. EEPROM writes must occur only during controlled commissioning/configuration changes, never periodically or in the normal control loop.

## 6. CiA402 State and Mode Control

The documented shared controlword sequence is:

| `0x6040` | Expected low status nibble | Meaning in manufacturer examples |
|---:|---:|---|
| `0x0000` | `0000` | Initialize/disable |
| `0x0006` | `0001` | Shutdown / ready step |
| `0x0007` | `0011` | Switch on |
| `0x000F` | `0111` | Enable operation |
| `0x0002` | Quick-stop state | Stop and remain enabled, per example |
| `0x0080` | Fault reset | Clear fault |

Do not advance the sequence using fixed delays alone. Read `0x6041`, decode the relevant state, enforce a timeout, and fail safe if the expected transition does not occur.

Operation mode:

| Object | Values |
|---|---|
| `0x6060:00` | `1` position, `3` velocity, `4` torque |
| `0x6061:00` | Read-only active mode display |

Set `0x6060`, then require `0x6061` to match before enabling motion.

`0x6041:00` is documented as a 32-bit dual-axis status value, but Phase 6A
deliberately names its extracted values the low and high halves rather than
left and right. The manufacturer's axis-order evidence remains insufficient
for a safety-relevant assignment. Manufacturer-specific bits vary by mode.
Important examples include:

- Bit 5: command quick-stop state.
- Bit 7: drive alarm.
- Bit 10: target reached, with mode-specific meaning.
- Bit 12/13: position/zero-speed conditions, depending on mode.
- Bit 14: stopped/running.
- Bit 15: external emergency stop.

Implement independent per-half extraction and a mode-specific status decoder.
Assign halves to physical axes only after hardware verification. Do not use
only the low nibble as the complete safety decision.

The Phase 6A pure decoder applies the complete standard masks documented in
`Docs/CIA402_DRIVE_DESIGN.md` independently to the neutral low/high halves and
retains every raw/vendor bit. This is host-tested software evidence only; it
does not resolve the physical half-to-axis mapping or vendor-bit meaning.

## 7. Velocity-Mode Objects

Recommended initial operation is velocity mode with independent 32-bit axis objects:

| Object | Type | Unit/range | Purpose |
|---|---|---|---|
| `0x200F:00` | U16 RW/S | `0` asynchronous, `1` synchronous | Left/right command application mode |
| `0x2008:00` | U16 RW/S | 1-1000 r/min, default 1000 | Motor maximum speed |
| `0x6083:01/02` | U32 RW/S | 0-32767 ms, default 500 ms | Left/right S-curve acceleration time |
| `0x6084:01/02` | U32 RW/S | 0-32767 ms, default 500 ms | Left/right S-curve deceleration time |
| `0x6085:01/02` | U32 RW/S | 0-32767 ms, default 10 ms | Left/right quick-stop deceleration time |
| `0x60FF:01/02` | I32 RW | -1000..1000 r/min | Left/right target velocity |
| `0x606C:01/02` | I32 RO | 0.1 r/min | Left/right actual velocity |
| `0x606C:03` | packed I16+I16 RO | 0.1 r/min | Combined actual velocity, low 16 left/high 16 right |

`0x60FF:03` is described at the end of both manuals as a packed left/right target, low 16 bits left and high 16 bits right, unit 1 r/min. However, an earlier Version 1.01 table incorrectly labels it read-only/current speed. Use `0x60FF:01` and `0x60FF:02` for the first implementation and **VERIFY ON HARDWARE** before using packed `0x60FF:03`.

The Quick Start text suggests a synchronous default, while the object dictionary lists `0x200F` default as zero/asynchronous. Read `0x200F` during commissioning and do not assume its factory value.

On the identified 2026-07-27 ZLAC8015D V4 fixture, a read-only SDO upload
returned `0x200F:00 = 1`. This verifies the stored value for that fixture only;
the exact synchronous application behavior remains unverified because no
target write or motion was authorized.

The first safe implementation should use asynchronous independent left/right targets, then evaluate synchronous packed updates only if simultaneous application is required and verified.

For the approved Phase 6B/M4 plan, `0x200F` is an explicit eligibility and
cleanup gate. Upload it before independent-axis RPDO operation. Do not infer
behavior from the fixture value `1`. Any reviewed temporary value must be
written only while both targets are zero, uploaded for exact readback,
behaviorally validated during separately authorized hardware work, and
restored to the original value without an EEPROM write.

## 8. Recommended PDO Mapping

The manufacturer supports only PDO transmission types 254 and 255.

- Type 254: asynchronous/event driven.
- Type 255: asynchronous/event timer.
- TPDO inhibit time uses 100 us units.
- TPDO event timer uses 500 us units according to the manufacturer examples.

Recommended initial mappings:

### RPDO1 - Independent target velocities

- COB-ID: `0x300 + N`.
- Communication parameter: `0x1401`.
- Mapping parameter: `0x1601`.
- Map `0x60FF:01`, 32 bits, followed by `0x60FF:02`, 32 bits.
- Eight data bytes contain left I32 followed by right I32, both little endian.
- Use transmission type 254.

This mapping is explicitly shown in both manufacturer documents and avoids the ambiguity of packed `0x60FF:03`.

### TPDO0 - Combined actual velocity

- COB-ID: `0x180 + N`.
- Communication parameter: `0x1800`.
- Mapping parameter: `0x1A00`.
- Map `0x606C:03`, 32 bits.
- Low 16 bits are left and high 16 bits are right; each signed half uses 0.1 r/min.
- Use type 255 plus a reviewed event timer for deterministic freshness, or type 254 plus an inhibit time if change-driven reporting is validated.

Additional recommended feedback PDOs, subject to mapping tests:

- One 8-byte TPDO containing `0x6041` status U32 plus `0x603F` fault U32.
- Optional telemetry PDO for bus voltage, temperature, current/torque, or input state at a slower rate.

PDO mapping should be configured in NMT Pre-operational, verified by SDO upload, saved only when changed, and activated by NMT Operational. Do not rewrite EEPROM on every boot unless required by a documented configuration-version migration.

### 2026-07-27 Fixture Readback

Read-only SDO uploads on the identified ZLAC8015D V4 fixture returned the
following factory mapping:

- RPDO1, COB-ID `0x201`, type `0xFF`, event timer raw value 1000:
  `0x6040:00/16` followed by `0x6060:00/8`.
- RPDO2, COB-ID `0x301`, type `0xFF`, event timer raw value 1000:
  packed `0x60FF:03/32`.
- RPDO3 and RPDO4 use COB-IDs `0x401` and `0x501`, type `0xFF`, event timer
  raw value 1000, and zero mapping counts.
- TPDO1, COB-ID `0x181`, type `0xFF`, event timer raw value 100:
  `0x6041:00/32` followed by packed `0x606C:03/32`.
- TPDO2, TPDO3, and TPDO4 use COB-IDs `0x281`, `0x381`, and `0x481`, type
  `0xFF`, zero event timers, and zero mapping counts.

All eight communication objects reported five implemented entries. Inhibit
and compatibility values were zero. Phase 6B.2A independently re-read the
complete reviewed communication inventory and all declared mapping
descriptors with 72 successful analyzer-correlated SDO uploads. Communication
subindex 6 was not probed.

No mapping was changed and no PDO was activated. This confirms that the
fixture's factory layout uses the unresolved packed subindex-3 velocity
objects; it does not verify axis order, sign, scale, or runtime payload
semantics. The raw evidence is archived in
`Docs/Evidence/Phase6B_Readiness/20260727/` and
`Docs/Evidence/Phase6B2A_ReadOnlyMapping/20260727/`.

The 2026-07-28 Phase 6B.3B read-only preflight specifically re-read the 15
fields required for exact volatile restoration. Target-side results confirmed
four-byte zero values for inactive descriptors `0x1601:02` and
`0x1A01:01/02`, zero count at `0x1A01:00`, and original TPDO2 event timer
`0x1801:05 = 0`. All 15 uploads succeeded without abort, timeout, retry, NMT
submission, EMCY, restart, or bus-off indication. The first analyzer exchange
through exchange 15 matched byte-for-byte with valid expedited-upload widths
and no `0x80` abort. No mapping or other drive value was written. Evidence is
archived under
`Docs/Evidence/Phase6B3B_ReadOnlyInventory/20260728/`.

In the later zero-target NMT observation on the same date, TPDO1 produced DLC
8 with payload `00 14 00 14 00 00 00 00` at approximately 50 ms intervals
while Operational. This matched the read-only raw values
`0x6041:00 = 0x14001400` and packed `0x606C:03 = 0`. TPDO1 stopped advancing
after return to Pre-operational. TPDO2..4 each produced one retained
zero-length frame even though their mapping counts were zero. Independent
analyzer evidence confirmed all four PDO identifiers, the TPDO1 payload and
46-53 ms timing, and the three zero-length frames. Stationary equal-axis data
does not resolve axis order, sign, scale, or vendor-bit semantics.

The Phase 6B.2A atomic repeat measured an 1819 ms Operational window. It
captured 37 identical TPDO1 frames over 1797 ms with 48-51 ms intervals and
one zero-length frame on each remaining TPDO identifier. Read-only feedback
was `0x6041 = 0x14001400`, `0x603F = 0`, `0x6061 = 3`, and zero for
`0x606C:01/02/03`. No motion or CAN fault was observed.

The 2026-07-28 Phase 6B.3E volatile mapping test independently verified TPDO2
type 255 with raw event timer 100. After the newer Operational heartbeat, the
analyzer retained 24 DLC-8 `0x281` frames containing two all-zero independent
axis values. Inter-frame intervals were 49–51 ms over 1149 ms. The STM32
independently retained the same 24-frame delta and measured 49–50 ms. The
session then returned to Pre-operational and restored/read back all 15 original
fields, including original TPDO2 event timer zero. No motion, torque reaction,
abnormal sound, EMCY, SDO abort, RPDO, target, mode, or controlword traffic was
observed. This validates zero-axis timing and payload shape, but stationary
equal values still do not resolve axis order, sign response, or scale.

## 9. Quick Stop, Emergency Stop, and Halt

`0x605A:00` selects quick-stop behavior:

| Value | Documented behavior |
|---:|---|
| `5` | Normal deceleration, remain in quick-stop state |
| `6` | Quick-stop deceleration, remain in quick-stop state |
| `7` | Immediate PWM removal/coast behavior, remain in quick-stop state |

Quick-stop deceleration time is configured independently for left/right using `0x6085:01/02`.

The manufacturer example uses `0x6040 = 0x0002` to enter quick stop and `0x000F` to leave it. `safety_manager` must own both actions. It must never restore `0x000F` automatically merely because communication resumes.

`0x605B`, `0x605C`, and `0x605D` define shutdown, disable-operation, and Halt reactions. These must be reviewed together with `0x605A`; normal ramping must not silently replace the reviewed emergency-stop policy.

The correct `0x605A` value and `0x6085` time depend on vehicle inertia, traction, power supply, brake resistor, and mechanical safety. Begin unloaded and speed-limited, measure the result, and approve the final fault matrix before loaded testing.

## 10. Hardware Emergency-Stop Inputs

Relevant objects:

| Object | Purpose |
|---|---|
| `0x2003:00` | Read X0/X1 input states in bits 0/1 |
| `0x2030:01` | X0/X1 active-level inversion |
| `0x2030:02` | X0 function; value 9 selects emergency stop; documented default 9 |
| `0x2030:03` | X1 function; value 9 selects emergency stop |
| `0x2026:03` | Per-input emergency-stop handling: low byte X0, high byte X1 |

`0x2026:03` per-input values:

- `0`: lock/hold torque after zero-speed stop, highest priority.
- `1`: damping mode by shorting motor phases, medium priority.
- `2`: PWM off/freewheel, lowest priority.

The final behavior also depends on `0x605A`. Confirm wiring, electrical levels, loss-of-wire behavior, and actual stop behavior using the complete drive user manual and hardware tests. A software CAN emergency stop is not a substitute for a required independent hardware safety circuit.

## 11. Brake Outputs

Relevant objects:

- `0x2030:04`: output active-level inversion; bits 2/3 correspond to B0/B1.
- `0x2030:07`: left B0 brake output.
- `0x2030:08`: right B1 brake output.

The documents use ambiguous and conflicting language for values 0/1, including whether the brake is "open," "closed," applied, released, or electrically energized. One example writes zero to `0x2030:07` and calls it closing/disabling the left brake, while another table reverses the wording.

**VERIFY ON HARDWARE BEFORE ANY MOTION:**

- Power-off mechanical brake state.
- B0/B1 electrical polarity.
- Meaning of values 0 and 1 in terms of physical brake applied/released.
- Effect of `0x2030:04` inversion.
- Whether output state persists through drive reset or CAN loss.
- Delay from drive torque availability to release and from command to physical application.

Only `brake_manager`, after `safety_manager` approval, may operate these outputs.

## 12. Faults and Diagnostics

`0x603F:00` is a 32-bit vendor fault register. Common codes include:

- `0x00000001`: overvoltage.
- `0x00000002`: undervoltage.
- `0x00000100`: EEPROM error.

Per-axis fault bits include overcurrent, overload, current deviation, encoder deviation, speed deviation, reference voltage, Hall, motor temperature, encoder, drive temperature, and target-speed error.

The manuals conflict on which 16-bit half represents left versus right. The object dictionary in the Quick Start guide indicates low 16 left/high 16 right, while Version 1.01 narrative text on PDF page 22 states the reverse. **VERIFY ON HARDWARE** by deliberately generating a safe, axis-specific fault or using a manufacturer-approved diagnostic method.

Fault reset is documented as `0x6040 = 0x0080`. Reset must be deliberate, authorized by `safety_manager`, and followed by a complete zero-command re-arm sequence.

Useful diagnostic objects:

| Object | Diagnostic value |
|---|---|
| `0x2031:00` | Software version |
| `0x2032:01/02/03` | Left/right motor and drive temperatures, 0.1 deg C |
| `0x2033:01/02` | Left/right motor running state |
| `0x2034:01/02` | Left/right Hall state; 0 or 7 indicates an error |
| `0x2035:00` | DC bus voltage, 0.01 V |
| `0x6077:01/02` | Left/right current/torque feedback, documented as 0.1 A |
| `0x6041:00` | Dual-axis statusword |
| `0x606C:01/02` | Actual velocities |
| `0x603F:00` | Last fault register |

## 13. Other Configuration Requiring Review

Do not modify motor-control tuning from firmware until the correct motor data and manufacturer approval are available.

| Object | Purpose / concern |
|---|---|
| `0x2007:00` | Power-up lock behavior; avoid automatic enable until safety behavior is confirmed |
| `0x2008:00` | Maximum speed; set to the approved vehicle/motor limit, not automatically to 1000 r/min |
| `0x200C` | Left/right motor pole pairs |
| `0x200E` | Left/right encoder line count |
| `0x2012` | Overload factor |
| `0x2013` | Motor/drive temperature thresholds |
| `0x2014/0x2015` | Rated and maximum current |
| `0x2016` | Overload protection time |
| `0x2018-0x2025` | Control-loop and observer tuning; commissioning-only |
| `0x2026:01` | Alarm PWM handling, documented default enabled |
| `0x2026:04` | Parking mode |
| `0x2026:05` | Commanded-speed resolution |
| `0x2026:06` | Speed-deviation protection |
| `0x2026:07` | Initial direction |
| `0x2027` | Brake/discharge resistor values, voltage thresholds, and enable |

Brake-resistor configuration must match the actual resistor and battery maximum voltage. Incorrect settings can create an overvoltage, thermal, or fire risk.

## 14. Proposed Initial Bench Profile

This is a commissioning starting point, not a final production configuration:

Project baseline constants are:

```c
#define ZLAC8015D_DEFAULT_NODE_ID      1U
#define ZLAC8015D_DEFAULT_BITRATE_KBPS 500U
#define ZLAC8015D_LEFT_DIRECTION_SIGN  1
#define ZLAC8015D_RIGHT_DIRECTION_SIGN -1
```

The direction signs express the intended logical chassis convention only. They
must be verified one axis at a time on an unloaded drive before being accepted
as physical motor direction evidence. The maintained definitions are in
`USER/Config/motion_config.h`, and the complete source/arbitration requirements
are in `Docs/CONTROL_REQUIREMENTS.md`.

1. Keep the vehicle unloaded, wheels clear, speed limited, and emergency power disconnect accessible.
2. Use one drive at node ID 1 and 500 kbit/s.
3. Wait for boot-up, enter Pre-operational, and read software version plus all critical configuration objects.
4. Confirm `0x200F`, `0x2000`, `0x2007`, `0x2008`, `0x605A`, `0x6083/84/85`, brake polarity, and emergency input configuration.
5. Enable a reviewed communication-loss timeout, initially within the manufacturer's 200-1000 ms recommendation.
6. Configure velocity mode, independent left/right RPDO targets, actual-speed TPDO, and status/fault TPDO.
7. Upload and compare every written configuration value.
8. Save to EEPROM once only if configuration changed and persistence is required.
9. Enter Operational, keep target zero, and execute the `0x0006 -> 0x0007 -> 0x000F` sequence while validating status after every step.
10. Test one axis at a time at very low speed, including direction and scale.
11. Test zero, normal deceleration, quick stop, communication loss, heartbeat loss, NMT stop, fault reset, drive reset, hardware emergency stop, and brake behavior.
12. Record CAN traces and measured timings as commissioning evidence.

## 15. Required Hardware-Verification Checklist

- [ ] Node-ID and bit-rate change activation procedure.
- [x] Actual heartbeat unit for `0x1017`: 0.5 ms/count on the identified 2026-07-22 fixture.
- [ ] Exact frames that reset `0x2000` communication-loss timer.
- [ ] `0x200F` behavior; the identified 2026-07-22 fixture read back raw value 1, but behavioral validation remains pending.
- [ ] Packed `0x60FF:03` access type, scale, and left/right order.
- [ ] `0x606C:03` scale, signed extraction, and left/right order.
- [ ] Left/right half ordering of `0x603F`.
- [ ] Per-axis interpretation of `0x6041` and all mode-specific bits.
- [x] TPDO2 type 255/event timer 100: DLC 8, zero-axis payload, 49–51 ms on
      the identified 2026-07-28 fixture.
- [ ] Quick-stop result for `0x605A` values 5, 6, and 7.
- [ ] Suitable production values for `0x6083`, `0x6084`, and `0x6085`.
- [ ] X0/X1 active level, open-wire behavior, and emergency-stop action.
- [ ] B0/B1 physical brake polarity and power-off behavior.
- [ ] Maximum speed, current, temperature, overload, and motor data.
- [ ] Brake-resistor hardware and `0x2027` settings.
- [ ] PDO configuration persistence and safe EEPROM write policy.
- [ ] CAN bus utilization and timeout margin under worst-case external-host traffic.

## 16. Phase 5 Software Integration Decision

The active non-actuating communications layer uses vendored CANopenNode v4.0,
local controller node ID `0x7F`, drive node ID 1, and the reviewed 500 kbit/s
standard Classic CAN allocation. The receive-focused process observes boot-up,
heartbeat/NMT state, EMCY, SDO responses, and four TPDOs. SYNC is disabled
because no reviewed synchronous-PDO timing requirement exists yet.

The project uses a 1500 ms heartbeat observation timeout and 300 ms TPDO
freshness timeout as software policies. The 2026-07-22 measurement resolves the
`0x1017` unit conflict for the identified fixture as 0.5 ms/count. The normal
controller does not write `0x6040`, `0x6060`, `0x60FF`, `0x2010`, motor
parameters, brake objects, or any other drive object. It does not send NMT or
SDO traffic at startup. The dedicated Debug-only commissioning artifact allowed
only fixed NMT transitions, whitelisted uploads, and exact volatile heartbeat
writes of 1000 or zero. The complete ownership and static object-dictionary
approach are documented in `Docs/CANOPEN_INTEGRATION.md`.

The 2026-07-22 evidence archive closes Phase 5 and M3 for non-actuating CANopen
communications. It covers boot-up, heartbeat, read-only SDO, PDO observation,
analyzer-injected EMCY reception, timeout, and restart. Remaining unchecked
items still require separate hardware work and no result in this batch claims
CiA402 motion, drive-originated fault behavior, brake behavior, electrical
bus-off, or worst-case load acceptance.

## 17. Phase 6B.2B Software-Only Transition Preparation

Phase 6B.2B defines but does not execute typed volatile candidates for
`0x6060:00 = 3` and `0x6040:00 = 0006/0007/000F`. No J-Link, CAN, SDO, NMT,
controlword, or mode operation was run for this software-only change. It does
not resolve the documented brake polarity/meaning, emergency-stop semantics,
packed-axis order, sign, scale, or vendor-bit interpretation. The current
prototype has no external E-stop or mechanical brake, so the first two items
are explicitly not required for its software-only commissioning tests. They
become required again if later hardware installs either device. Any future
hardware execution needs fresh operator safety confirmation and a separately
authorized step-by-step plan; it cannot claim Phase 6, Phase 6B, or M4 complete.

## 18. Approved Phase 6B/M4 SBUS Bench Scope (PENDING)

The approved plan authorizes G0-G6 software preparation and verification only.
It does not claim that the M4 Bench capability, runtime pipeline, executor, or
hardware evidence exists. G7-G10 remain separately authorized hardware work.

The future M4 Bench capability must be Debug-only, default OFF, and mutually
exclusive with Commissioning. SBUS is the sole M4 motion-intent source and must
pass through `control_arbiter`, `safety_manager`, `safety_cia402_adapter`,
`cia402_drive`, a typed mailbox, and the CANopen owner. External CAN,
diagnostics, debugger variables, and commissioning controls cannot write M4
mode, controlword, target, NMT, or brake operations.

Each first-motion authorization is an immutable, start-anchored, non-renewable
one-shot generation: one axis only, absolute target no greater than 10 rpm,
duration no greater than 1 second, and the other target exactly zero. Repeated
SBUS frames, periodic cycles, mailbox publications, superseding requests, or
timestamp wrap cannot extend the expiry. Expiry or invalidation clears pending
nonzero content and requires neutral plus a new re-arm generation.

The exact fixture must explicitly declare emergency-stop, brake, and watchdog
applicability. Not applicable is not equivalent to required-and-healthy.
Required-but-unknown evidence or an applicability mismatch inhibits motion.
This requirement preserves the current prototype history without assuming
that a future fixture has the same hardware.

Every G7-G10 session requires a dated authorization identifying the fixture,
operators, commit, ELF hash, enabled capability, allowed one-shot generations,
and pre-authorized cleanup. With usable CAN, cleanup requires verified zero,
verified `0x0006`, NMT Pre-operational, and exact restoration/readback of
`0x200F`, all temporary PDO values, and `0x1017`. With unusable CAN, do not
claim a successful wire-level stop; remove physical power immediately, then
restore and read back configuration only after controlled communication
recovery.

Electrical bus-off must use real error-injection equipment, and the fault-reset
case must use a reviewed non-destructive stimulus. Without both, the evidence
may support only M4 partial status. No current evidence closes G7-G10 or M4.
