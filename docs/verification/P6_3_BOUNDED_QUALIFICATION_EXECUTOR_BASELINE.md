# P6.3 Bounded Qualification Executor Baseline

Evidence date: 2026-09-04. Result: **PASS — P6.3 COMPLETE**.

## Scope and result

P6.3 adds a separately configured, Debug-only, default-OFF qualification
artifact. It reuses the Phase 5 single CANopen owner and the P6.2 typed ZLAC
codecs; it does not add another transport, lifecycle, generic SDO client, or
raw-frame API. The build option is:

```text
ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION=OFF
```

Qualification and P5.6 commissioning builds are mutually exclusive. Release
configuration with either transmit-capable artifact is rejected. The normal
Debug and Release builds contain no qualification executable and retain the
default-deny CANopen transmit boundary.

The exact qualification gate permits only:

- node-1 NMT Start, Stopped, and Pre-operational;
- expedited `0x6060:00 = 3` with exact `0x6061:00 = 3` readback;
- expedited `0x6040:00 = 0x0006`, `0x0007`, or `0x000F`;
- expedited I32 `0x60FF:01/02` targets bounded to -10..10 rpm;
- fixed-width readback of `0x603F:00`, `0x6040:00`, `0x6041:00`,
  `0x6060:00`, `0x6061:00`, `0x606C:01/02/03`, and `0x60FF:01/02`.

Each authorization matches the complete CAN ID, DLC, and eight data bytes and
is consumed by one successful send. Only local kernel backpressure
(`EINTR`, `EAGAIN`, or `ENOBUFS`) retains that same authorization. Broadcasts,
reset NMT, adjacent indices/subindices, wrong widths or values, RPDOs, targets
outside the bound, packed target `0x60FF:03`, persistent objects, and arbitrary
frames remain denied.

`QualificationSession` permanently enters `cleanup_required` after any
failure and exposes no recovery or rearm operation. A target sequence is usable
once, starts with both independent targets written and read back as zero,
permits one nonzero independent target for at most 1000 ms, supervises fresh
heartbeat and TPDO1 with no current EMCY or CAN error every 10 ms, and always
attempts a verified zero after a nonzero write. Cleanup remains callable after
heartbeat loss and attempts both verified zero targets, Shutdown, and NMT
Pre-operational.

## Managed-vcan evidence

The namespace-local managed-vcan suite passed with:

```text
INFO: managed namespace vcan0 is up
INFO: P6.3 managed-vcan frames=110 prohibited=0 failures=0
```

The scenarios cover the allowed sequence, non-renewable replay rejection, SDO
timeout with no retry and late-response quarantine, failed target readback with
best-effort zero, TPDO staleness during the nonzero interval with verified
zero, stale-heartbeat rejection without ordinary TX, cleanup while feedback is
stale, SIGTERM interruption, and interface loss. The independent gate test
also covers the exact allowlist, wrong node/COB-ID/DLC/data, adjacent object
fields, reset and broadcast NMT, RPDOs, out-of-range targets, persistent
objects, and authorization consumption/backpressure behavior.

The archived preflight is
`docs/verification/evidence/p6_3_20260904/safety_preflight.yaml`. It explicitly
limits the run to an ephemeral namespace and `vcan0`. No physical CAN adapter,
drive, RK3588 target runtime, deployment, configuration write, or motor motion
was used in P6.3.

## Verification

| Check | Result |
|---|---|
| Qualification Host Debug build | PASS |
| Qualification pure-software CTest, excluding privileged socket/vcan tests | PASS, 32/32 |
| Qualification transmit-gate runtime test | PASS |
| Managed-vcan qualification scenarios | PASS, 110 frames, 0 prohibited, 0 failures |
| Default Host Debug regression | PASS, 25/25 applicable tests |
| Default Host Release regression | PASS, 25/25 applicable tests |
| Release qualification configure guard | PASS, rejected |
| P5.6/qualification mutual-exclusion guard | PASS, rejected |
| P5.6 commissioning gate regression | PASS |
| P5.6 managed-vcan regression | PASS, 13 frames, 0 prohibited |
| ASan/UBSan qualification build and CLI/stack tests | PASS |
| ASan/UBSan gate and managed-vcan tests | PASS, 110 frames, 0 prohibited, 0 failures |
| LLVM 22.1.8 format and scoped clang-tidy | PASS |
| CANopen dependency verification | PASS, both pinned trees clean |
| RK3588 Debug direct cross build | PASS, 69/69 clean rebuild steps |
| RK3588 qualification ELF audit | PASS, aarch64 PIE, `/lib/ld-linux-aarch64.so.1`, no RPATH/RUNPATH |
| RK3588 Release cross build | DEFERRED: release requires a clean committed source snapshot |
| Physical HIL or deployment | NOT RUN; not authorized or required for P6.3 |

The final RK3588 Debug qualification ELF is:

```text
out/build/cross/rk3588-p63-debug/tools/zlac_qualification/robot-control-zlac-qualification
SHA-256 486c3fc02c6cf939ba57e6897b6be4d9f800cfe1f080f5f3e956c79db9644cc4
```

It was built with the locked image
`sha256:f2198e31e27c084bc2deff761e124fa9d7ce580a8d986c7885fd62bb1701e7dd`,
network disabled, the source mounted read-only, and the validated real RK3588
Ubuntu 22.04 sysroot mounted read-only.

## Acceptance

Update 2026-09-10: clean-source normal RK3588 Debug/Release cross checks and
the separate Debug qualification cross build pass. Fresh default Debug/Release
and P5.6 regression and symbol isolation also pass. Exact source provenance
and the remaining physical requirements are in `P6_7_CLOSURE_BASELINE.md`.

P6.3 acceptance is satisfied. The executor is bounded, default absent,
Debug-only, exact-frame gated, permanently inhibiting after failure, and
isolated from normal and P5.6 transmit policies. Arbitrary SDO, raw-frame,
EEPROM, reset, RPDO, and periodic-transmit APIs do not exist.

P6.4 is next. This result grants no authorization for deployment, physical CAN
writes, zero-target drive transitions, or motor motion.
