# P10.1 offline control cycle — 2026-09-16

Full host regression: Debug runtime 36/36, Release 35/35, ASan/UBSan runtime
36/36, existing P6 Debug 75/75. No skips. Local ASan uses detect_leaks=0.
The final three focused XML files rerun the changed control-cycle test after
SBUS interlock generation handling and the test-only exhaustive-switch cleanup;
unchanged suites were not rerun merely to repeat already passing results.

Final cross: 74 steps, locked Docker image and real RK3588 Ubuntu22.04 sysroot,
BUILD_TESTING=ON, Debug runtime=ON. cross-control-cycle.py reproduces this recipe
and requires unused output/snapshot paths. cross-metadata.json records the dirty
source snapshot, image, sysroot and binary identities. The compiled-source list
was compared byte-for-byte with that snapshot; later verification documentation
is not part of the compiled-code identity. Run its sha256sum check from repo root.

ELF audit proves architecture, interpreter, required target library versions,
no RPATH and linked offline control/SBUS/domain code, without device adapters or
CAN write gates. The binary was not executed on target hardware.

Initial static diagnostics and the Docker socket sandbox denial are preserved.
Final scoped clang-tidy is clean with warnings treated as errors. Scope includes
application/control, changed domain headers/implementations, runtime and its test,
new control-cycle tests and domain_tests lines110–310; unrelated legacy optional
accesses remain outside the changed range. Existing pragma once and explicit
32-bit diagnostic flags are retained. A local fixture annotation explains its
positional numeric parameters. No global source-level check disabling was added.

The actual verification commands are in commands.txt; XML reports carry results,
and results.json is a checked summary. SHA256SUMS covers these evidence files.
No UART, target, physical CAN, drive configuration, motion or deployment occurred.
P10.2 protocol closure and P10.3 newly authorized HIL remain separate gates.
