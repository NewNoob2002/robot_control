# TPDO feedback diagnosis — 2026-09-11

## Synchronous packed-target repair trial

The synchronous-mode route is now supported by one bounded left-axis HIL
trial: 0x200F remained 1, 0x60FF:03 commanded left +5/right 0, and the
existing TPDO1 reported nonzero left speed. Six independent/packed SDO
sample groups and nearby TPDOs agree within their sampling offsets; right
samples remain zero. Target-to-zero was 3001.000 ms, captures match exactly,
and zero/Shutdown/Pre-operational cleanup passed without CAN errors/drops.
The operator confirmed left-only rotation, normal stop, stationary right wheel and no abnormal sound. See
[synchronous trial evidence](evidence/p6_sync_feedback_20260911/RESULT.md).

First-motion now preserves verified synchronous mode and uses packed
targets; it no longer selects asynchronous mode. This proves the combined
mode/target route on this fixture, not mode alone or every firmware revision.
The separately selected RPDO qualification path has now passed left and right
physical trials, with operator-confirmed normal stopping and mapping restoration.
Remaining stop/loss paths now use synchronous packed targets in software, but
physical requalification is blocked by execution approval timeouts. See the
[repair record](P6_SYNC_PACKED_PDO_REPAIR.md).

## Prior asynchronous diagnostic

The bounded left-axis diagnostic has isolated the zero feedback to the drive
combined-speed object under asynchronous, enabled-drive conditions. Six running
SDO groups measured independent left raw speeds 50,46,46,53,48,48 and independent
right zero, while direct SDO uploads of0x606C:03 and mapped TPDO speed stayed zero.
The existing TPDO stream and host decoding are not the source of those zeros.

The 0x200F mode hypothesis is supported but not yet isolated from enable state.
Do not claim0x606C:03 is documented as synchronous-only. Full motion-feedback
qualification, watchdog timing and physical loss tests remain open.

The Debug-only single-target executor now makes bounded read-only diagnostic
samples during motion, preserving the original zero deadline and cleanup.
Host, sanitizer, cross-build, ELF and target isolated-vcan checks passed. One
physical run completed with matched captures and verified zero/restoration.

See [raw sample table, limitations and verification](evidence/p6_tpdo_probe_20260911/RESULT.md)
and the retained [prior diagnosis](evidence/p6_review_motion_20260911/TPDO_DIAGNOSIS.md).
