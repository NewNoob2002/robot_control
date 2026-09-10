# P6.1 Contract, Fixture, and Read-only Baseline

Status: **COMPLETE**

Preparation date: 2026-09-04

## Result

P6.1 has an exact read-only object manifest, requirement matrix, completed
safety preflights, and all 119 planned JCAN values. On 2026-09-04, the first
authorized attempt completed positions 1 through 96 and stopped without retry
at a position-97 timeout. A separately authorized continuation retried
`0x2000:00` once, succeeded, and completed positions 98 through 119 once each.
The operator explicitly accepts the first command-session RK3588 capture for
positions 1 through 96; the continuation retains all 46 frames in a standalone
raw file.

No SDO download, NMT command, RPDO, reset, interface change, periodic
transmission, parameter modification, or motion command ran. Both cleanups
passed. Review of the actual values retained one manufacturer contradiction:
`0x1018:00` reports 5 while both manuals define only `00/01/02`. The raw
count is preserved, but undocumented subindices are not assumed or required.

## Requirement matrix

| Requirement | Evidence | Current result |
|---|---|---|
| P6.1-R01 Define scope and evidence layout | This document and `evidence/p6_1_20260904/README.md` | Pass |
| P6.1-R02 Define exact object, width, source, use, and classification | `evidence/p6_1_20260904/read_manifest.yaml` | Pass |
| P6.1-R03 Preserve default-deny and Phase 5 artifact policies | No source, CMake, or executable change | Pass |
| P6.1-R04 Record exact fixture and software identities | Completed preflight, scan, configuration, and target status | Pass |
| P6.1-R05 Capture identity and firmware raw values | Complete inventory and value-review report | Pass; documented `0x1018:00/01/02` captured, raw count contradiction retained |
| P6.1-R06 Capture all RPDO/TPDO communication and mapping values | Complete inventory and accepted first-session capture | Pass |
| P6.1-R07 Capture eligibility, state, fault, mode, velocity, and restoration values | Continuation positions 97 through 111 plus RK3588 raw capture | Pass |
| P6.1-R08 Resolve emergency-input and brake applicability | Installed-device declaration and continuation positions 112 through 119 | Pass |
| P6.1-R09 Prove no state-changing operation ran | `cleanup.md`, before/after configuration and target status | Pass |
| P6.1-R10 Provide exact rollback value before every later write | `fixture_inventory_complete.yaml` | Pass; later writes still require their own authorization and preflight |

## Exact read scope

The machine-readable authority is `read_manifest.yaml`. Its compact PDO rows
mean the Cartesian product of every listed index and every listed subindex; no
adjacent index or subindex is implied. Width is the expected expedited SDO data
width, not permission to download the object. The manifest expands to 111
mandatory objects plus at most 8 conditional safety-I/O objects, for a maximum
of 119 one-attempt uploads.

The mandatory inventory covers:

- identity and firmware: `0x1000`, `0x1001`, `0x1009`, `0x100A`, `0x1018`, and
  `0x2031`;
- all four RPDO and TPDO communication objects and all four RPDO and TPDO
  mapping objects;
- eligibility/restoration: `0x2000`, `0x2007`, `0x2008`, `0x200F`, `0x605A`,
  `0x6060`, and independent zero-target candidates `0x60FF:01/02`;
- observations: `0x603F`, `0x6041`, `0x6061`, and `0x606C:00/01/02/03`;
- conditional safety I/O: `0x2003:00`, `0x2026:03`, and
  `0x2030:01/02/03/04/07/08` only when the fixture declaration says the
  corresponding emergency inputs or brakes are installed.

`0x6060` and `0x60FF:01/02` are included beyond the minimum acceptance list
because later slices intend to write them. This is the smallest way to enforce
the Phase 6 rule that every future write has an exact rollback or safe-zero
baseline before authorization.

## Physical-session gate

Before any read, copy `safety_preflight.template.yaml` to a dated immutable
preflight and replace every placeholder. Required current facts are:

- RK3588 board identity, deployed binary path and SHA-256, source revision;
- drive model, hardware/firmware identity, node ID, bitrate, power state;
- JCAN serial and channel plus a fresh configuration baseline;
- named operator, unloaded/clear wheel state, and accessible independent power
  disconnect or physical stop;
- emergency-input and mechanical-brake presence or explicit not-applicable
  decisions;
- exact one-shot SDO list, timeout, no-retry rule, monitors, and cleanup.

Read-only SDO uploads are active bus writes. The session stops on any warning,
identity mismatch, unexpected frame, abort, timeout, width mismatch, capture
gap, configuration change, or cleanup error. It does not silently retry.

## Evidence layout

The dated directory reserves these names for the authorized run:

```text
docs/verification/evidence/p6_1_YYYYMMDD/
  README.md
  read_manifest.yaml
  safety_preflight.yaml
  fixture_inventory.yaml
  rk3588_raw.log
  jcan_scan.json
  jcan_get_config_before.json
  jcan_raw_capture.json
  bus_validation.yaml
  test_result.yaml
  cleanup.md
  SHA256SUMS
```

`fixture_inventory.yaml` must retain, for each exact object, request frame,
response frame or abort, width, raw little-endian bytes, decoded value, source,
intended use, access classification, rollback role, and review status. Inferred
or historical values are not accepted as current raw evidence.

## Source review

Widths and access classifications were checked against both repository vendor
PDFs on 2026-09-04. The manuals agree on the selected widths. Their physical
left/right names remain non-authoritative where the Phase 6 plan requires
neutral low/high-half names until motion evidence resolves mapping.

The current repository did not need source code or build-system changes for
P6.1. P6.3 owns the separate Debug-only Phase 6 executor; extending the Phase
5 commissioning artifact here would violate the planned slice boundary.

## Residual gate

P6.1 is complete. The 2026-09-04 sessions produced all 119 planned values,
exact rollback values, an explicitly reported recovered timeout, accepted
RK3588 evidence for positions 1 through 96, standalone RK3588 evidence for
positions 97 through 119, and verified cleanup. The `0x1018:00=5` versus
manual-defined `00/01/02` discrepancy remains recorded without probing or
inventing undocumented subindices. The detailed review is
`P6_1_COMMAND_TRANSCRIPT_ACCEPTANCE_AND_VALUE_REVIEW.md`.
