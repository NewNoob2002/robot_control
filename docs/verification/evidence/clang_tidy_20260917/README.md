# Workspace clang-tidy assessment — 2026-09-17

The original untracked configuration is preserved as `original.clang-tidy`.
It was unsuitable as-is: FunctionCase=camelCase is an invalid option value,
the src/include header filter misses this project's module directories, and
blanket style groups conflict with existing APIs, protocol constants and POSIX
ownership boundaries. `original-runtime.log` records an actual translation-unit
run; --verify-config alone did not catch the invalid naming enum.

The root policy now follows project naming and ownership, covers module/test/tool
headers, separates blocking defects from advisory findings, and retains scoped
exceptions for generated CANopen names, linker ABI names, explicit enum widths
and pragma once. The usage and exception rationale are in
`../../../development/STATIC_ANALYSIS.md`. No C/C++ source was renamed, formatted
or automatically fixed.

## Reproduction and scope

- Tool: clang-tidy/run-clang-tidy22.1.8.
- Input inventory: `coverage.json`;69 unique CMake translation units and no
  uncovered project C/C++ source files. A merged database at
  out/build/tidy-workspace uses the first real CMake entry for each filename,
  in the documented database order; it does not invent flags.
- Command: `run-clang-tidy -p out/build/tidy-workspace -j 2 -quiet`.
- Final full output: `accepted-scan.log`; counts and config hashes: `summary.json`.
- Actual naming/lifetime probe: `policy-probe.log`. It contains a GoodType class
  with private value_, a global kAcceptedConstant, a bad_type class and a function
  indexing a string after std::move. Valid names produce no diagnostics; bad_type
  produces a naming warning and use-after-move produces a failing error.

Earlier pilot/narrowed logs are development diagnostics, not the final verdict.
Each source is covered in one valid build mode; this does not qualify every
preprocessor variant, aarch64 compilation or hardware. The policy changes do not
alter any accepted109-input C/C++ artifact. No new binary/HIL acceptance is claimed.

## Findings retained for review

- Unchecked optional access: Result::value documents ok() as a caller precondition;
  commissioning relies on the parser's mutually exclusive action contract. Two
  unit-test expressions dereference fixed-profile decode results. These checks
  remain enabled: a contract explanation is not proof of every caller.
- can_probe main lacks a top-level exception handler; the advisory remains visible.
- Generated _OD and the logging configuration's reserved guard spelling remain
  reported. No global reserved-identifier suppression was added.
- Copy/move and stop_token passing suggestions need normal code review. Several
  endl calls flush operator-visible phase cues; blindly replacing them with a
  newline could delay cues and change HIL coordination.

Successful command exit means no configured blocking diagnostics, not zero
warnings or a completed remediation of all existing code. This task establishes
the shared policy and its coverage; existing advisories remain explicit.

## Pilot log storage

Six earlier pilot logs, including original-runtime.log, are preserved under their
original paths in [the September 18 archive](../archives/september18-capture-and-pilots.tar.gz).
See its MANIFEST.json entries for exact byte counts and SHA256 values. Extract
into a separate scratch directory when inspecting those historical diagnostics.
Final accepted-scan.log, coverage, configurations and probe remain directly available.
