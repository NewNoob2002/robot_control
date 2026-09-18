# Workspace C/C++ static-analysis policy

The root `.clang-tidy` is the shared policy for all project-owned C11/C++20
sources and headers, including optional tools and tests. `.clang-format` owns
formatting; AGENTS.md owns architectural, ownership, documentation and safety
requirements. clang-tidy does not enforce Python, shell, CMake or Markdown rules.

## Rules and exceptions

- Types use CamelCase; functions, methods, namespaces, variables and parameters
  use lower_case. Private members have a trailing underscore. Constants use
  lower_case; existing kCamelCase constants remain valid.
- Analyzer, bug-prone, performance and portability checks are enabled, with
  selected modernization and class-lifetime checks. Compiler/configuration,
  analyzer and selected lifetime findings fail the command. Other findings
  remain warnings requiring review; command success does not mean no findings.
- Broad readability/modernize/cppcoreguidelines groups are deliberately avoided:
  protocol constants, POSIX interfaces and existing function signatures do not
  justify bulk rewrites. Similar-parameter warnings are omitted because ordered
  protocol fields deliberately share types.
- Explicit enum widths and pragma once follow existing GCC/Clang project policy.
  The insecureAPI DeprecatedOrUnsafeBufferHandling check is omitted because its
  Annex K replacement advice is unsuitable for the Linux libc target. Other
  analyzer security checks stay enabled; bounded buffer access remains required.
- CANopen callback and linker wrapper names retain their external ABI spelling.
  Generated OD.c/OD.h inherit defect checks but disable identifier naming through
  `communication/canopen/od/.clang-tidy`. Do not edit generated names to satisfy
  a style check. Third-party sources and build outputs are not project style targets.
- Do not globally disable a defect check to hide existing findings. Any local
  NOLINT must name the check and explain the contract or false positive. Do not
  use automatic `-fix` across accepted control code.

## Running checks

The validated tool is clang-tidy/run-clang-tidy 22.1.8. Check the version when
reproducing results: wildcard check sets change between LLVM releases. An older
Ubuntu package has not been qualified against this policy.

Use CMake compile databases, never guessed include paths or feature definitions:

```sh
cmake --preset host-test
run-clang-tidy -p out/build/host-test -j 2 -quiet \
  '/robot_control/(application|communication|domain|input|platform|service|tools|tests)/'
```

The final regex selects main source files; HeaderFilterRegex only controls included
headers. Adjust the checkout directory name if necessary. Root configuration is
found automatically; do not pass `-config-file`, which would bypass the generated
directory's inherited exception. Editor integrations must use the matching CMake
compile database and the workspace configuration.

Default builds do not cover optional tools. For a complete check, configure four
separate Debug build directories with BUILD_TESTING=ON and
CMAKE_EXPORT_COMPILE_COMMANDS=ON, enabling one of the following options in each:

| Build mode | CMake option set to ON |
| --- | --- |
| Control HIL | ROBOT_CONTROL_BUILD_CONTROL_HIL |
| Runtime owner | ROBOT_CONTROL_BUILD_CANOPEN_RUNTIME |
| Phase 6 qualification | ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION |
| Commissioning | ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING |

Run the same command against each database. Preserve each mode's actual flags;
do not combine incompatible feature switches into one build. Header-only code is
checked through the translation units that include it. Future unreferenced headers
need a real consumer/test before coverage can be claimed.

The 2026-09-17 validation merges existing CMake entries by source filename solely
for an efficient baseline scan (one valid mode per source): 69 translation units,
including generated OD.c, with no missing project C/C++ source files. It is not
exhaustive testing of every feature/preprocessor combination. See
`../verification/evidence/clang_tidy_20260917/coverage.json` and `accepted-scan.log`.
The policy probe verifies valid names, a rejected type name and use-after-move
error promotion. Existing CI build/static/script checks remain separate; this
change does not claim a new remote clang-tidy gate.
