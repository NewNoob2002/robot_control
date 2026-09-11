# Synchronous packed PDO repair — 2026-09-11

The zero-speed feedback defect is resolved for the tested synchronous route.
With 0x200F verified as 1, packed target 0x60FF:03 drives the selected wheel
and existing TPDO1 reports nonzero packed speed. Independent SDO speed,
packed SDO speed and TPDO speed now agree within sampling-time differences.
This verifies the combined mode/target route on this fixture, not the
isolated effect of the mode bit or other firmware revisions.

## Physical evidence

| Trial | Command | Matching capture frames | Target-to-zero | Operator |
|---|---|---:|---:|---|
| Synchronous SDO left | +5 rpm left, zero right, 3000 ms | 234 | 3001.000 ms | Left only; normal stop; no abnormal sound |
| RPDO left | 201#0F0005000000 then 201#060000000000 | 290 | 3001.002 ms | Left only; normal stop; no abnormal sound |
| RPDO right | 201#0F0000000500 then 201#060000000000 | 292 | 3000.994 ms | Right only; normal stop; no abnormal sound |

Each trial ran once with the RK3588 application as the transmitter, silent
JCAN plus target candump, zero error/drop counters, matching frame/counter
accounting, and verified zero-speed/terminal-state cleanup. Each RPDO trial
sent exactly two RPDOs and restored the checked factory RPDO1 mapping.
Reported durations are measurements, not hard-real-time guarantees.

Raw captures, operator confirmations, reproducible analyzers and manifests:

- [Synchronous SDO evidence](evidence/p6_sync_feedback_20260911/RESULT.md)
- [RPDO left](evidence/p6_rpdo_feedback_20260911/left/analysis.json)
- [RPDO right](evidence/p6_rpdo_feedback_20260911/right/analysis.json)

## Implemented bounded qualification behavior

- TPDO1 remains 0x6041:00/32 + 0x606C:03/32, eight bytes.
- Explicit --rpdo-once temporarily maps RPDO1 to 0x6040:00/16 +
  0x60FF:03/32, six bytes. It verifies the exact original COB-ID, type,
  timer and mapping first; writes only in Pre-operational; checks each
  write/readback; and restores the baseline even after partial setup.
- Mode 0x200F must read 1. First-motion and stop/loss qualification no
  longer switch it to zero. No EEPROM or permanent parameter writes occur.
- Initial enable still requires both targets zero and the existing separate
  CiA402 transitions. The RPDO interval sends one already-enabled target,
  then Shutdown with packed zero. A failed or short send is not retried.
  SDO zero cleanup remains available independently of RPDO delivery.
- The packed gate admits only one moving wheel within +/-10 rpm. Physical
  trials above used +5 rpm. Simultaneous nonzero wheels and negative RPDO
  motion have not been physically qualified.
- Stopping tolerates deceleration but still waits for fresh matching status
  with zero packed velocity under the transition deadline. Startup rejects
  any nonzero feedback immediately.
- The watchdog test now uses passive TPDO zero-speed evidence at the end of
  its TX-free interval. The first subsequent TX clears both targets, instead
  of sending an SDO speed query that might renew a retained motion target.

The implementation remains Debug-only/default-OFF qualification software.
This is not a production periodic RPDO controller, automatic rearm, or a
claim of atomic internal controlword/target execution by the drive. Vendor
command synchronization remains distinct from CANopen SYNC transmission.

## Artifact and software validation

| Artifact | SHA256 prefix | Evidence |
|---|---|---|
| Initial synchronous SDO trial | 3885a78bda2d | Host 57/57, sanitizer 57/57, clean cross build, ELF audit, target isolated vcan, physical left trial |
| RPDO left/right trial | 0e835fd8a9bc | Host 60/60, sanitizer 60/60, clean cross build, ELF audit, target isolated vcan, both physical trials |
| Synchronous stop/loss remainder | fa1944fdba2d | Host 60/60, sanitizer 60/60, clean cross build, ELF audit, target isolated vcan; physical tests pending |

Latest software logs and manifest are in
[remainder evidence](evidence/p6_sync_remainder_20260911/manifest.json).
Default Debug/Release builds remain unchanged and build successfully.
Clang-tidy completed without errors; eight existing advisories remain.
git diff --check passed. Initial host test failures are preserved: the
deceleration fixture needed a subsequent zero TPDO, and the RPDO test peer
needed an explicit 0x201 receive filter. These were software-test failures,
not physical test retries.

Remaining gates include synchronous stop/loss physical requalification,
physical cable/power-loss tests, and any production or simultaneous-wheel
operation. Do not infer those passes from the three successful trials.

The first watchdog execution request and its one permitted request retry
both timed out in automatic approval before process creation. Local one-shot
markers confirm no remainder trial began. Test authorization remains recorded;
the blocker is execution permission review, not an HIL failure. See
[the blocker record](evidence/p6_sync_remainder_20260911/execution_blocker.json).
