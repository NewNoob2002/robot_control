# Third-Party Dependency Provenance

Status date: 2026-07-31.

Normal builds must not fetch mutable branches. A dependency is accepted only
after its upstream URL, immutable commit, license, integration method, and local
patch status are recorded here and in the applicable ADR.

## Existing snapshots

### CANopenNode

- Path: `components/CANopenNode`
- Upstream: <https://github.com/CANopenNode/CANopenNode>
- License: Apache-2.0 (`components/CANopenNode/LICENSE`)
- Local file count at baseline: 71
- Baseline content digest:
  `1e3e5e8ffd43dfe4df8e40345cab3fe51f0beb757c89ac88c63b20f2ac3879d5`
  (SHA-256 over the sorted list of per-file SHA-256 records)
- Version status: **unresolved mixed/unversioned snapshot**

The local snapshot cannot be attributed to one upstream commit from the
available files. For example:

- local `CANopen.c` exactly matches upstream commit
  `8c09a433efdb85c15f141e177127d481732c66ac`;
- local `doc/CHANGELOG.md` exactly matches upstream commit
  `f41bfdcb5d0d432455e5d4df0a16cd5520a8fd57`.

Those files resolve to different commits. The directory is therefore not an
acceptable release dependency as-is.

### EasyLogger

- Path: `components/EasyLogger`
- Upstream: <https://github.com/armink/EasyLogger>
- License: MIT (`components/EasyLogger/LICENSE`)
- Local file count at baseline: 433
- Baseline content digest:
  `c34343500332a71e37ce0e09ce52a86ad4a7f417b15dbb5ebf6f08eb68762c08`
- Version status: **unresolved mixed/unversioned snapshot**

The local `easylogger/src/elog.c` matches upstream commit
`980eac7383e26a98837a6b42e9cefcd219b15166`, while the license file first
matches another historical commit. The directory also contains large unrelated
MCU demonstrations. ADR-0002 excludes EasyLogger from the initial Linux design.
Do not link or port it unless a later ADR demonstrates a requirement unmet by
the project journald-oriented logger.

## Selected dependency pair

The initial CANopen integration spike shall use:

| Dependency | Immutable commit | Upstream description |
|---|---|---|
| CANopenLinux | `f1348d4072cdabea4c3435a13c721ac29ab4cc91` | `v4.0-25-gf1348d4`, 2025-08-18 |
| CANopenNode | `ef9ac3a2279e34855a20c787fc1bc48bc995ec22` | CANopenLinux submodule commit, 2025-08-04 |

Both use Apache-2.0. The pair is chosen together because CANopenLinux pins that
exact CANopenNode commit as its submodule, eliminating an inferred API match.

## Integration policy

1. Replace the unversioned CANopenNode snapshot only in the dependency
   integration phase, after reviewing the diff and legacy compatibility.
2. Prefer Git submodules for the paired upstream repositories because their
   relationship is already expressed by CANopenLinux.
3. Do not edit third-party files directly. Carry necessary changes as minimal,
   documented patches and attempt upstream compatibility first.
4. CI and release builds operate without network access after dependencies are
   checked out.
5. Generate a license/SBOM report before the first release.

