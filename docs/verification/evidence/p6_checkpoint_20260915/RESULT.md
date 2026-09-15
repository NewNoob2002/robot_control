# September 15 deferred-soak checkpoint

**LOCAL CI PASS; CROSS REVALIDATION UNAVAILABLE; PHASE 6 OPEN.**

| Check | Result |
| --- | --- |
| Qualification Debug | 68/68, no skips |
| Qualification Clang ASan/UBSan | 68/68, no skips |
| Default Debug | 26 P5.1 + 2 SocketCAN cases, all pass |
| Default Release | 28/28, no skips |
| Read-only commissioning | 36/36, no skips |
| ShellCheck 0.10.0 / checksum-pinned Hadolint 2.12.0 | PASS |
| Dependency / Phase 1 / sysroot / ELF-audit script regressions | PASS |
| Emergency-input / soak offline regressions | PASS |
| Clang static analysis | 0 errors; 24 advisory lines reviewed |
| Evidence archive hashes and current links | PASS |
| New RK3588 cross build / physical HIL | NOT RUN; locked cross image absent; no new physical test authorized |

Commands and raw output are in local-ci/; JUnit reports and test_result.json
retain counts and limitations. Native tools are GCC 16.2.1 and Clang 22.1.8;
GitHub's Ubuntu 22.04 job supplies the separate supported-host check.

Review reproduced one malformed inhibitor-release defect before the fix.
MSG_TRUNC now enforces the exact one-byte packet on both receive paths.
The host and sanitizer vcan cases pass; previous physical artifacts are not
relabeled as tests of this new helper. Soak SIGHUP isolation has host/target
offline evidence; v3 remains failed, v4 never started, and JCAN framing is open.

The previous cross/sysroot artifacts remain intact. The missing locked Docker
image was not replaced by an unpinned toolchain. This limitation does not waive
final target/cross qualification. GitHub CI attached to the pushed SHA is the
remote result; see ../../P6_CHECKPOINT_REVIEW_20260915.md.
