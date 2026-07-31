# Phase 0 Repository Baseline

- Date: 2026-07-31
- Host: Ubuntu 26.04 x86_64
- Target declared: RK3588 aarch64, Debian 11, Linux 5.10.160
- Legacy reference commit: `c34042e68aa23fcd789a63a1693a507296935032`
- Docker image ID: `sha256:14cae4e1ab5aa8ee611e1efe4d20408bce17734b52297b9fa2c9c12a1742f677`
- Docker image environment: Ubuntu 22.04.5, aarch64 GCC 11.4.0, CMake 3.22.1

## Initial repository findings

- No working root CMake project, toolchain file, tests, CI, service, or application sources existed.
- `scripts/build.sh` references missing build files and an incorrect fixed workspace mount.
- `config/`, `platform/hal/`, and `user/*` were empty placeholders.
- `components/CANopenNode` and `components/EasyLogger` were unversioned snapshots.
- `.git/` was an empty read-only directory; Phase 0 initialized a new `main` repository without deleting source files.

## Integrity manifest

`MANIFEST.sha256` records all non-Git, non-runtime files present after the Phase 0 documentation baseline.
It is evidence, not a dependency lock. Regenerate intentionally when the baseline is superseded.
