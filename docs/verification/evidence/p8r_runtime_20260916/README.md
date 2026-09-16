# P8-R software evidence — 2026-09-16

Final host: runtime Debug 35/35, default Release 34/34, runtime ASan/UBSan 35/35,
existing P6 regression 74/74; all executed without skips. Local sanitizer uses
detect_leaks=0. Final XML contains the vcan submission-to-peer timing samples.
Initial runtime test logs are earlier limited checks; final suite logs/XML are authoritative.

Cross: locked image and original RK3588 Ubuntu22.04 sysroot, 70 steps, explicit
runtime Debug build. cross-runtime.py records the exact isolated snapshot/CMake
recipe; it expects unused out/p8r-cross-source and build destinations when replayed.
The source attestation is a dirty development snapshot, not a clean Release claim.
compiled-source-sha256.txt is checked from the repository root at this milestone;
SHA256SUMS is checked from this evidence directory. Historical manifests are not
expected to match later source revisions.

ELF validation checks interpreter/dependencies/versions, runtime linkage and the
retained upstream deny gate. The runtime fixture is cross-linked, not executed on
a physical target. No UART, physical CAN, drive configuration or motion occurred.

The initial static findings are retained beside the clean final report. GNU ld
wrap identifiers have local documented exceptions; no global checks were disabled.
See ../../P8_R_RUNTIME_BASELINE.md for scope, commands, layout limitations and P10 gates.
