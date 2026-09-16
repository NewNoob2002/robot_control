# P10.2 local software / vcan evidence — 2026-09-16

Scope and limitations: [baseline](../../P10_2_CONTROL_LOOP_BASELINE.md).
Base source: 476a000a928b7d19d26bd98b19624309edd757e0; dirty worktree source snapshot,
not a clean production release. No hardware operation or target execution.

- Final focused 4/4, Debug37/37, Release35/35, sanitizer37/37, P6 75/75;
  counts and timestamps are retained in results.json and compressed JUnit.
- sanitizer.xml.gz / sanitizer-test.log.gz are the **FAILED** initial 36/37 run:
  existing observer CLI select wait timed out. observer-retry and sanitizer-final
  record separate successful runs, without changing that test. Root cause is
  not established; no sanitizer diagnostic appeared in the failed output.
- first.xml.gz preserves the initial incorrect down-link peer receive oracle:
  the wrapper reports EIO, while SO_ERROR holds ENETDOWN. The peer clears the
  socket error before virtual link-up and draining. No production socket fix.
- late-signal.xml.gz also preserves the actual nonzero-after-pending-SIGTERM
  failure, fixed in Lifecycle. loop/integrated/focused XML are intermediate
  passing stages; focused-final includes the shutdown reporting deadline case.
- static-initial retains optional/index/dead-store test findings, subsequently
  fixed. static-final and static-lifecycle pass. static-loop-final retains a
  later broad-header check's pre-existing Result::value optional-access warnings
  in platform/linux/error.hpp; static-loop-scoped uses the original changed-area
  header filter and passes. This is scoped static validation, not repository-wide
  suppression or a claim that unchanged Result has been redesigned.
- build logs/configurations, commands.txt, selector/format/actionlint logs preserve
  local verification. Empty logs indicate silent successful commands only where
  their result is stated in the baseline; use test XML for counts.
- cross-control-loop.py reuses the locked image and actual target sysroot, with
  network disabled/read-only container and a unique source snapshot. A replay
  needs a fresh snapshot destination; existing snapshots must not be overwritten.
  cross-metadata.json identifies the source archive, image, sysroot and four ELF
  hashes. cross.log.gz records all79 build steps and sysroot validation.
- audit-control-loop.py / elf-audit.txt verify aarch64 ABI dependencies/versions,
  no RPATH, required application/runtime/CAN/UART/source symbols, upstream transmit
  deny and absence of qualification/commissioning write gates. No target execution.
- compiled-source-sha256.txt covers changed compiled code and build/CI routing
  files, compared byte-for-byte with the cross snapshot after verification.
  Documentation and raw evidence were finalized later and are covered by SHA256SUMS.

Remote push and CI are checked for the eventual commit separately; local success
is not a substitute for remote CI. P10.3 remains separately authorized work.
