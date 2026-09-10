# P6.4 Zero-target HIL Preparation

Prepared: 2026-09-07. Updated: 2026-09-07T09:15:02Z.
Status: **HISTORICAL PREPARATION; P6.4 COMPLETE**.

Current result: [P6.4 executor and HIL baseline](P6_4_ZERO_TARGET_EXECUTOR_BASELINE.md).
The original findings below are retained as preparation history. The final
normal and post-enable SIGTERM HIL runs passed; this file no longer describes
current blockers.

The user authorized P6.4 hardware writes in this conversation. This record
preserves that authorization separately from execution readiness. Passive
captures completed. Attempted SDO reads have no execution evidence because
approval timed out or was interrupted; no drive response is established. No
target deployment or active zero-target sequence has executed. P6.4 is not passed.

## Initial preflight evidence (historical; current state in linked baseline)

Evidence directory: `evidence/p6_4_20260907_preflight/`.

| Check | Result |
|---|---|
| RK3588 identity | SSH alias `robot-dev`; hostname `lubancat`; machine ID `6923ab3301fb4a8d816759b04ec6bf0a`, matching P6.1 |
| SocketCAN | `can0` DOWN, CAN STOPPED, RX/TX counters zero; not ready for capture |
| Target processes | No match for robot-control, candump, or cansend in the read-only process check |
| JCAN discovery | Serial `207F346D5650`, matching P6.1 |
| JCAN configuration | Raw baseline archived; no configuration write |
| JCAN periodic tasks | No active or recent task; no session cleanup error |
| JCAN profile | Optional old application-policy record; current MCP explicitly does not enforce it. No profile change is required merely to use P6.4 operations |
| Existing aarch64 ELF | SHA-256 matches the P6.3 record: `486c3fc02c6cf939ba57e6897b6be4d9f800cfe1f080f5f3e956c79db9644cc4` |
| Source | HEAD `1d1314d25269d38f9679cd2c15e23df65afcd2a4`; dirty worktree, including pre-existing untracked Phase 6 files |
| Current physical fixture | User confirms operator present, drive powered, wheels raised and independent power-disconnect emergency stop available. Operator name, brake condition and wiring changes remain unspecified |

JCAN calls returned `ok=true` and no warnings. The initial preflight incorrectly
treated an old optional profile as a current MCP restriction. The user clarified
the current contract, and tool metadata confirms that `jcan_bus_profile_status`
reads the profile without enforcement. `jcan_send_once` validates protocol
legality, and `jcan_periodic_start` supports caller-selected IDs and periods
with an explicit count of 1..1500. Application authorization and P6.4 bounds
remain the responsibility of the caller; the old profile is not a readiness
blocker. Raw speed codes are not a newly verified bitrate.

## Temporary can0 configuration

The recorded P5.7 target baseline used Classical CAN, 500000 bit/s and
`restart-ms 100`; see `evidence/p5_7_20260904/target_hil_sdo_rerun_postflight.txt`.
The following commands are prepared for the RK3588. They have not been executed
in this preflight and do not install persistent boot/network configuration:

```sh
sudo ip link set dev can0 down
sudo ip link set dev can0 type can bitrate 500000 restart-ms 100 fd off
sudo ip link set dev can0 up
ip -details -statistics link show dev can0
timeout 5s candump -ta -e can0
```

Require UP/LOWER_UP, ERROR-ACTIVE, bitrate 500000 and no increasing error/drop
counters before qualification. Preserve output and heartbeat/TPDO cadence.
This is interface setup and passive application-level observation, not a CiA402
enable sequence. Bus-off recovery does not authorize motion recovery.
The original preflight observed the interface DOWN and `restart-ms 1`; record
any actual change and restore the agreed interface baseline only after verified
drive cleanup.

## Original software readiness findings (resolved by current implementation)

The P6.3 artifact supplies individual bounded operations; it is not yet a
complete P6.4 qualification runner. Source inspection found:

1. `QualificationSession::qualify_zero_target_cia402`, `wait_nmt_state`,
   `wait_dual_state`, `require_zero_velocity_feedback`, and
   `cleanup_zero_target_cia402` are declared in `qualification.hpp`, but are not
   defined in `qualification.cpp`. These declarations predate this preflight.
2. `main.cpp` has no zero-target sequence CLI operation. Its controlword branch
   confirms the SDO download, without waiting for a newer matching dual status.
   Only the nonzero `target_once` branch invokes session cleanup on exit.
3. Every separate CLI invocation starts a new lifecycle and waits at most
   1000 ms for boot-up plus current heartbeat. `ObservationStore` deliberately
   keeps heartbeat non-current until boot-up has been observed. An already
   powered drive sending ordinary heartbeat alone does not satisfy this gate.
4. The CLI TPDO freshness limit is 100 ms, while the accepted P6.1 inventory
   records TPDO1 event time 100 ms. Actual cadence/jitter must be checked before
   treating this deadline as a usable HIL bound. Do not silently relax it.

Do not chain the existing per-operation CLI invocations to claim P6.4. Finish
one owner-process sequence with newer-status gating, zero feedback checks,
bounded startup/termination and verified cleanup; test it on managed vcan
before deployment. Preserve the existing boot-generation invariant and do not
send NMT reset or cycle drive power merely to satisfy it. A coordinated boot
requires an explicit physical procedure and current operator participation.

Required software checks before active HIL: normal sequence, both status halves,
old/wrong/missing feedback, wrong mode or target readback, nonzero feedback,
EMCY/CAN error, SDO timeout, SIGTERM, cleanup failure and non-renewable execution.
Then run applicable host, sanitizer, LLVM, default/P5.6 isolation and RK3588
Debug cross checks, and record the new ELF checksum. This preflight did not
rerun the historical build or vcan suite.

## Exact proposed active scope

One drive, node 1, Classical CAN at the existing 500000 bit/s fixture baseline.
RK3588 is the qualification requester; JCAN is the independent passive
observer during the sequence. No other requester may operate concurrently.
The following table defines allowed payload values, not an executable script
and not permission to run while any readiness gate remains open.

| Purpose | CAN ID | DLC | Exact payload |
|---|---|---|---|
| Target subindex 1 zero | `0x601` | 8 | `23 FF 60 01 00 00 00 00` |
| Target subindex 2 zero | `0x601` | 8 | `23 FF 60 02 00 00 00 00` |
| NMT Operational, node 1 | `0x000` | 2 | `01 01` |
| Velocity mode 3 | `0x601` | 8 | `2F 60 60 00 03 00 00 00` |
| Shutdown | `0x601` | 8 | `2B 40 60 00 06 00 00 00` |
| Switch On | `0x601` | 8 | `2B 40 60 00 07 00 00 00` |
| Enable Operation | `0x601` | 8 | `2B 40 60 00 0F 00 00 00` |
| NMT Pre-operational, node 1 | `0x000` | 2 | `80 01` |

Readbacks use only the existing fixed qualification read allowlist:
`0x603F:00` (4 bytes), `0x6040:00` (2), `0x6041:00` (4),
`0x6060:00` (1), `0x6061:00` (1), `0x606C:01/02/03` (4 each),
and `0x60FF:01/02` (4 each). Each upload request is standard `0x601`, DLC 8,
`40 <index-low> <index-high> <subindex> 00 00 00 00`; response COB-ID is
`0x581`. Reject wrong object, width, abort, timeout or unexpected response.
No implicit retry.

Before the state sequence, refresh current drive identity against P6.1 through
an explicitly bounded read manifest and record mode/targets/status/faults and
applicable safety inputs. Historical values are expectations, not live reads.
The current mode must already be 3 and both targets zero for this narrow
same-value restoration path; any different value requires stopping and
reviewing the restoration scope before writing.

Proposed sequence, to be encoded and verified in the single-process runner:

1. Start both bounded raw captures before any request; obtain boot/current
   heartbeat within the reviewed startup procedure. Verify identity, faults,
   initial mode, targets and exact-zero independent/packed velocity.
2. Write and read back both targets as zero before enabling any drive state.
3. Send Operational and require a newer node-1 Operational heartbeat.
4. Set mode 3 and read both mode request/display as 3.
5. Issue `0x0006`, `0x0007`, `0x000F` once each. Advance only on a newer
   matching status for both neutral halves, within the selected monotonic
   deadline; check zero feedback throughout and require physical no-motion
   observation. Preserve raw status/vendor bits.
6. Clean up immediately: write/read both zero targets, send Shutdown and
   verify both states, send Pre-operational and verify a newer heartbeat,
   upload final mode and zero targets, stop processes/captures, verify no
   residual transmission and unchanged adapter configuration.

The sequence itself needs only nine download requests (four zero-target
writes, one mode write, four controlword writes including cleanup), and two
NMT requests on its normal path. Exact upload count, cleanup reserve, startup
deadline, per-step deadline and capture bounds must be emitted by the reviewed
runner before active execution; none is to be inferred from this prose.
The initial proposed zero tolerance is exactly zero in raw independent and
packed feedback; a nonzero sample fails this plan rather than broadening it.

## Recovery and evidence gate

On mismatch, stale status, timeout, EMCY, unexpected traffic or termination:
inhibit further enable commands and enter only the bounded approved cleanup.
If usable-CAN cleanup cannot be completed and verified, the present operator
must disconnect drive power. Never claim a wire-level stop from process exit.
Record cleanup failure; no automatic retry, restart or re-enable.

No nonzero target, packed target write, RPDO, broadcast/reset NMT, fault reset,
EEPROM, watchdog/speed-limit/brake-setting change or periodic TX belongs to
this P6.4 scope. P6.1 recorded communication-loss protection disabled and
unresolved physical brake semantics; neither is an available stop guarantee.

Before proceeding, resolve all of these gates:

- Current physical fixture: presence, power, raised wheels and independent
  power-disconnect path confirmed by the user. Record the operator name and
  remaining applicable fixture details before the active session.
- Completed/tested single-process P6.4 runner and reviewed boot procedure.
- Reviewable staging destination and checksum-verified qualification artifact.
- Exact temporary `can0` bring-up configuration and recovery to the observed
  baseline; target interface changes must be explicitly covered.
- Caller-selected P6.4 capture bounds and complete overlapping RK3588/JCAN
  raw coverage, including cleanup; no capture gaps accepted by assumption.
  An old optional MCP profile does not block this work.
- Current identity/value read manifest, exact request budget and final
  `safety_preflight` with every placeholder resolved.

Each active session must preserve `safety_preflight`, `bus_validation`,
`test_result`, deployed checksum, raw captures from both observers, decoded
transition timestamps, physical no-motion observation and final cleanup.
The negative-path HIL evidence required by the Phase 6 plan remains separate
from any future successful normal zero-target run.
