# Ground-function checkpoint before application handoff — 2026-09-20

This checkpoint archives the current qualification source and physical evidence
before a separate manually launched test application is developed. It does not
promote the bounded HIL executable to a continuous vehicle application.

## Subsequent software checkpoint — 2026-09-21

The pre-app acceptance below is preserved. The separate SBUS app, configuration,
logging and device-configuration tool are now implemented and software-tested;
IPC is deferred. Current scope and remaining target validation are in
[TEST_APP_HANDOFF](../plans/TEST_APP_HANDOFF.md) and
[TEST_APP_BASELINE](TEST_APP_BASELINE.md). The open/next-work lists below describe
the September 20 handoff boundary, not the current app implementation status.

## Accepted nodes

- G1: separate nonblocking diagnostics and raw trace collection.
- G2: one-hour SBUS/ControlLoop zero-target soak; previous failed attempts retained.
- G3: bounded raised single-wheel signs, dual-wheel motion, turns and differential
  approach; separately recorded X1/RF/UART/SIGTERM cases.
- G4/G5: stationary ground X1 and six bounded unloaded ground functions: forward,
  reverse, left/right rotation and left/right differential, with normal stopping.

The authoritative trial index is [POST_SOAK_GROUND_READINESS](../plans/POST_SOAK_GROUND_READINESS.md).
Ground left differential A1 and its failed cleanup remain failed; the independent
baseline restoration and revised A2 acceptance are separate records. Protective
exit codes, raw feedback excursions and retrospective acceptance remain distinct.
All consumed trial runners remain consumed. Latest ground right differential
operator disposition is stationary/no abnormal sound/drive power OFF.

## Open or deferred nodes

- Remaining fault injection, including equipment-dependent single-axis faults
  and electrical CAN cases; ground fault stopping remains separate.
- Exact wheel-speed scale, measured mechanical stopping time/distance and an
  independent stopping/holding solution. Reported ground distances are estimates.
- Rockchip delayed CAN TX/worker concurrency, continuous motion, loaded operation
  and final five-hour integrated stability validation.
- Independent test app, startup configuration, general running diagnostics, IPC
  receiver/client and ROS2 adapter; no production/systemd deployment is accepted.

## Source and validation

Base: a233889 on codex/phase9-sbus-development. Frozen accepted HIL SHA256:
51f94c1f51945f3d28fa945d58f14cb15ad4ec7a4cab353331a2f39e7ca16451.
All 123 compiled inputs still match
evidence/g3_differential_preparation_20260920/compiled-inputs.json. Existing full
Debug53/53, sanitizer53/53, scoped static, locked cross/ELF and target device-free
smoke evidence therefore remains applicable; no new physical run occurred.

Delivery checks on 2026-09-20:

- Both HIL builds: ninja reports no work to do.
- Focused cycle/runtime/trace/motion-gate/zero-gate/differential-audit: Debug6/6
  and ASan/UBSan6/6 pass; local sanitizer uses detect_leaks=0.
- Existing P10.3 verifier:1473 inventory files,1941 checksums,255 Python scripts.
- G3/G4/G5/control-soak checksums:1455 pass; new archived Python syntax passes.
- git diff --check passes. One pre-existing new target-result.tar.gz exceeds
  20MiB; it is retained intact as raw evidence.

Delivery commit/tag and remote CI are recorded after push in the local checkpoint
archive. The archive includes a Git bundle, source/evidence archive, accepted
aarch64 HIL/diagnostics binaries and hashes; build trees/sysroot/secrets are excluded.

## Next work

Develop a separate Debug/default-OFF test app and a local IPC client, reusing
ControlLoop, current layout preflight and safety policy. Preserve HIL limits and
historical records. Validate software/vcan first; a new entry point needs its own
target regression before handoff for motion. This checkpoint grants no new
hardware authorization.
