# Phase 6 verified checkpoint — 2026-09-10

This checkpoint commits the implemented and verified Phase 6 work through the
operator-accepted manual TPDO speed test. **Phase 6 remains in progress.**
Remaining hardware tests wait until this checkpoint is pushed and remote CI passes.

## Milestone disposition

| Item | Status at this checkpoint | Evidence / scope |
| --- | --- | --- |
| P6.1 fixture and contract | PASS | 119 accepted inventory values; identity-count contradiction retained |
| P6.2 protocol semantics | PASS | Pure typed status, faults, modes, targets, speed and controlwords |
| P6.3 bounded executor | PASS | Debug-only/default-OFF, fixed one-frame gate, host and managed-vcan validation |
| P6.4 zero-target lifecycle | PASS | Normal transitions and post-enable SIGTERM at zero target |
| P6.5 first motion | PASS | Separately tested positive +5 rpm independent left/right targets, raised/unloaded |
| P6.6 NMT Stop, Shutdown, Disable Voltage, Quick Stop | PASS | Exact recorded unloaded HIL trials; no general loaded-operation claim |
| Manual speed-first TPDO feedback | PASS — operator accepted | Right wheel manually turned both ways; left observed stationary; normal brake/sound |
| Watchdog timing and recovery | OPEN | Stop/restart observed in trial 2; corrected trial 4 verified initial motion and subsequent zero, not exact trigger timing or safe rearm |
| Physical heartbeat/TPDO loss, moving SIGTERM, interface loss | OPEN | Software cases exist; remaining physical stimuli are not accepted by this checkpoint |
| P6.7 final phase acceptance | OPEN | Remaining physical requirements/applicability decisions and final regression required |

See the [evidence index](evidence/README.md) for each accepted trial and archived
history. Individual failed attempts remain preserved; a later pass does not
erase them. Detailed scope remains in the [Phase 6 plan](../plans/PHASE6_ZLAC8015D_QUALIFICATION.md).

## Accepted manual TPDO result

The operator explicitly accepted the [manual TPDO test](evidence/p6_6_20260910_manual_tpdo_trial_1/RESULT.md)
and its small left-speed excursions. Chassis vibration is the operator's proposed
explanation, not a measured cause, and is not encoded as a firmware safety fact.

The test configured TPDO1 with packed 0x606C:03/32 first, then status 0x6041:00/32,
with type 255 and event timer raw 100. It recorded 1,205 speed samples at a median
50.007 ms interval. Both captures match all 2,042 frames. The operator turned only
the right wheel clockwise and counter-clockwise. No target or controlword was
written; the original mapping and heartbeat were restored with exact readbacks.

The kernel RX counter exceeds the two agreeing captures by three packets/24 bytes.
This remains a separate accounting issue, not a reversal of the operator's TPDO
acceptance. The test does not establish a watchdog timing bound, production rearm
policy, or the physical cause of the earlier status-first packed-speed behavior.

## Software and artifact verification

[Current software evidence](evidence/p6_6_20260910_manual_tpdo_preparation/RESULT.md):

- Host Debug qualification: 53/53 tests passed.
- LLVM 22.1.8 ASan/UBSan qualification: 53/53 passed.
- Default Debug and Release: 28/28 each; P5.6 isolation: 36/36.
- Managed-vcan includes 33 communication-loss and 15 manual-TPDO scenarios.
- Pinned RK3588 Debug build and target-sysroot ELF audit passed.
- Static analysis: no errors; 21 documented advisories reviewed.
- Firmware-source hashes match the tested/deployed manual-TPDO artifact. The
  test harness later received an explicit value capture for Clang 14 compatibility;
  the original artifact attestation remains unchanged.

Qualification artifact SHA256:
8ca2c0250736e792ecb0b7067973ce2547f7e2d6fb9c38d0147a5788ade34c8a.

GitHub Host CI now explicitly covers Phase 6 qualification and sanitizer builds,
managed-vcan execution without silent skips, and evidence-integrity checks, in
addition to the existing default build/static checks. The check runs attached to
the pushed commit are the authoritative remote result; local logs alone do not
satisfy the requested remote-CI gate. Real RK3588 HIL is not run in GitHub Actions.

## Evidence consolidation

Thirty-six superseded preparation/trial directories (575 files) were consolidated
into four deterministic archives after verifying every file's bytes and hash.
Accepted trials, current software evidence and all watchdog attempts remain
readable as ordinary files. Three duplicate preparation runners and four generated
Python cache files were removed. Unique raw captures, failures, configuration
readbacks and original authorization records are preserved.

The [archive manifest](evidence/archives/MANIFEST.json) maps every archived path and
removed duplicate to its retained source. The common RK3588 qualification ELF
audit now lives under scripts/build/ rather than a historical evidence directory.
Repeated milestone narratives in README, AGENTS and the closure baseline are
replaced by this current checkpoint and the evidence index.

Two pre-existing NMT Stop Markdown checksum mismatches were reviewed; the old
manifest and discrepancy record are retained alongside the refreshed hashes.
No raw capture checksum mismatch was found. The promoted qualification ELF
audit's RPATH rejection was corrected after a negative regression reproduced
its prior false pass. RPATH/RUNPATH rejection tests and the actual RK3588 artifact
audit now pass with the corrected script; no firmware source changed.

## Work after the remote-CI gate

1. Review the accepted TPDO configuration for use in the next bounded motion test;
   the hand-rotation session restored the original mapping.
2. Complete the [watchdog timing/recovery procedure](P6_6_WATCHDOG_TIMING_RECOVERY_REVIEW.md)
   with verified passive speed feedback and an explicit recovery policy.
3. Run separately authorized physical loss/interruption tests and close remaining
   applicability decisions. No prior authorization permits an automatic repeat.

Production motion services, full SBUS/M4 control, ROS2, persistent drive writes,
loaded operation, packaging and production deployment remain outside this checkpoint.
