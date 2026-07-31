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
- Version status: **checksum-pinned vendored snapshot; upstream commit remains
  unresolved**

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
MCU demonstrations. The amended ADR-0002 permits only the following
checksum-pinned core subset:

| File | SHA-256 |
|---|---|
| `easylogger/src/elog.c` | `775e0cbde6a7ebb9ef69bceb400042a55f98f02372b88b5cf61c5fef5d131e25` |
| `easylogger/src/elog_utils.c` | `937eaf98151cb5fa25f102621802637d71ccadd56028098e3a45978a0707d9d0` |
| `easylogger/inc/elog.h` | `e073ebceedaa064c058ecccf0ef1b06250975d937a30eee14f57de1fd9eff281` |
| `LICENSE` | `c80023edf0b6ab08a88059549d4a79daa1f3ac3dc6a6350f25dff42fd9a88e44` |

Normal builds compile only this subset plus the project-owned synchronous Linux
port and configuration. MCU demonstrations, bundled ports, plugins, async
output, buffering, and color are excluded. Replacing the snapshot with a clean
immutable upstream checkout remains a release-readiness task.

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
