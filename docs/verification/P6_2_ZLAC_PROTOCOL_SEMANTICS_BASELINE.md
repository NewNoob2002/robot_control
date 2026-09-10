# P6.2 ZLAC8015D Protocol Semantics Baseline

Evidence date: 2026-09-04. Result: **PASS — P6.2 COMPLETE**.

## Scope and result

P6.2 adds only pure ZLAC8015D protocol semantics under `domain/drive`. The
implementation reuses the existing CiA402 status decoder and provides typed,
little-endian codecs for:

- dual `0x6041` status and `0x603F` fault values;
- signed `0x6061` mode display;
- independent `0x606C:01/02` I32 and packed `0x606C:03` I16+I16 feedback in
  0.1 rpm units;
- independent `0x60FF:01/02` I32 targets bounded to -1000..1000 rpm;
- reviewed `0x6040` transition values `0x0006`, `0x0007`, and `0x000F`.

Raw status/fault/packed values and signed mode data are retained. All byte
decoders reject malformed widths, target codecs reject out-of-range values,
and independent values use only neutral subindex names. Low/high halves remain
neutral and every relevant observation reports
`physical_axis_mapping_known=false`; no physical left/right mapping API exists.

The change introduces no Linux header, socket, CANopen owner access, hardware
operation, or transmit authorization. Packed target `0x60FF:03`, fault reset,
quick stop, arbitrary object access, and P6.3 executor behavior are not added.

## Verification

| Check | Result |
|---|---|
| Host narrow `domain_behavior_lock` | PASS, 1/1 |
| Host Debug pure-software regression, excluding the three SocketCAN/vcan runtime tests | PASS, 25/25 |
| Host Release pure-software regression, same exclusions | PASS, 25/25 |
| ASan/UBSan `domain_behavior_lock` | PASS, 1/1 with leak detection disabled for the execution environment |
| LLVM 22.1.8 format checks | PASS for new source/header and the added test block using the existing file's 2-space style |
| LLVM 22.1.8 scoped clang-tidy | PASS; one pre-existing `ArbiterReason` enum-size advisory remains outside P6.2 |
| Serena diagnostics for new source/header and P6.2 test range | PASS, no diagnostics |
| RK3588 Debug cross build and ELF audit | PASS, 46/46 build steps; interpreter, dependencies, symbol versions, Phase 3 symbols, and no-RPATH checks pass |
| RK3588 Release cross build | DEFERRED: the release script correctly rejected the dirty source snapshot before configuration |
| `git diff --check` | PASS |

The unexcluded Host Debug and Release CTest attempts each reported 25 passing,
two expected vcan skips, and one existing `socketcan_socket_lifecycle` failure.
In the restricted environment, PF_CAN socket creation fails before the test's
expected missing-interface lookup, so its operation/error assertions cannot be
reached. P6.2 has no dependency on that path; the project-standard
pure-software exclusion run is the applicable regression result.

RK3588 Release must be rerun after the P6.1/P6.2 worktree is committed, because
the release build deliberately accepts only a clean source snapshot. No safety
gate was bypassed.

Update 2026-09-10: the deferred Release cross compilation passed using a
clean committed review checkout whose snapshot hash exactly matches the
reviewed Phase 6 working-tree snapshot. Both RK3588 Debug and Release passed
46/46 build steps and ELF audits. See `P6_7_CLOSURE_BASELINE.md` and its
archived build metadata; the main checkout history was not changed.

## Acceptance

- Documented mode and controlword values, the captured `0x14001400` status,
  signed I16/I32 boundaries, little-endian positive/negative targets, both
  independent subindices, unknown status/mode values, malformed widths, and
  target range rejection are covered by host tests.
- Physical axis names remain unavailable until one-axis hardware evidence
  resolves the mapping.
- No socket, hardware dependency, or transmit authorization is introduced.

P6.2 acceptance is satisfied. P6.3 is the next delivery slice and requires a
separate Debug-only executor; it does not inherit authorization for CAN writes
or hardware operations from this software-only work.
