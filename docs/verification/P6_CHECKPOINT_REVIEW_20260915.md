# Phase 6 deferred-soak checkpoint review — 2026-09-15

Status: **REVIEWED; LOCAL CI PASS; CROSS UNAVAILABLE; PHASE 6 OPEN**.

## Scope and findings

Reviewed the accumulated changes since f3d88d4: shared SDO local cancellation
and generation checks, bounded Quick Stop recovery, NMT Stop cleanup in
Pre-operational, external-loss supervision/recovery, interface-down checks,
the private can0 inhibitor and CLI integration, fixtures and soak scripts.
Caller/ownership review followed the single lifecycle owner, failure paths,
transmit gates and default-artifact isolation. Existing failed HIL was retained.

One concrete defect was reproduced and corrected: SOCK_SEQPACKET receive with
a one-byte buffer accepted a longer packet beginning with R as a valid release.
The new malformed-release vcan check failed at the expected interface-down
assertion before the fix. Both protocol receive sites now use MSG_TRUNC and
require the original packet length to be exactly one. The same isolated test
passes after the fix. This source change has no new physical-HIL claim.

Soak child-session isolation, in-cycle capture liveness and actual exit-code
recording pass the host and target offline checks. V3 remains failed and V4
never started. JCAN USB receive framing remains unresolved. See
[the deferral record](P6_SOAK_SESSION_REPAIR.md).

## Validation and cleanup

The final command/results record is retained with the
[checkpoint evidence](evidence/p6_checkpoint_20260915/RESULT.md). Debug and
sanitizer CTest runs must execute all managed-vcan cases without skips; default
Debug/Release and commissioning builds verify isolation. The workflow now also
runs emergency-input and soak script regressions and explicitly requires the
inhibitor vcan case.

Pre-cleanup source/evidence is backed up under ignored out/checkpoints/. Large
evidence files are compressed only after byte/hash verification; the shared
archive manifest retains original member paths. Generated Python caches and
obsolete build material are recorded in the cleanup ledger. No unique trial
record, consumed marker or operator observation is discarded.

GitHub Actions attached to the eventual pushed SHA is the remote result. A
green CI run does not close deferred soak, missing hardware/fault qualification
or the kernel retry-worker investigation. No physical CAN, motion, deployment
or persistent drive change is part of this checkpoint publication.
