# Phase 6 review fixes — 2026-09-10

Software disposition: PASS. Physical requalification: NOT RUN. Phase 6 remains
open. This change addresses review priorities 1 and 2 only; it does not deploy
software or authorize motor operation.

## Diagnosis and resulting contracts

1. **Standalone activation bypass.** The public NMT/controlword helpers previously
   required only ready state and a fresh heartbeat. An acknowledged SDO download
   could therefore succeed without zero-target preflight or a newer matching
   drive state. The CLI now rejects standalone Operational, velocity-mode,
   Switch On and Enable Operation before creating a lifecycle. The session also
   rejects standalone activation; mode selection requires a prepared sequence.
   Activation remains in the complete zero/first-motion/stop qualification paths,
   with existing zero preflight, newer dual-state checks and bounded cleanup.
   Standalone Shutdown and the existing library Disable Voltage/Quick Stop
   operations retain their inhibit-only submission contract; no new motion
   feedback requirement was added to those inhibit operations.

2. **TPDO layout assumption.** Every zero/motion preflight, including the library
   run_target_once path, now reads the exact live contract below before target
   writes or enabling. A mismatch fails closed. Stop-trial setup may temporarily
   enable heartbeat before this check; its existing rollback remains active.

   | Object | Required raw value |
   | --- | --- |
   | 0x1800:01 | 0x00000181 (enabled node-1 TPDO1) |
   | 0x1800:02 | 255 |
   | 0x1800:05 | 100 |
   | 0x1A00:00 | 2 |
   | 0x1A00:01 | 0x60410020 (dual status first) |
   | 0x1A00:02 | 0x606C0320 (packed speed second) |

   The local TPDO1 expected DLC must also be 8. The speed-first manual-test
   mapping is rejected by motion paths, not silently interpreted as status-first.
   These constants match the recorded fixture and the existing manual-test
   baseline restoration. Raw timer 100 is a configuration value, not a claimed
   physical timing bound. No TPDO remapping or expanded transmit whitelist was
   added. Six mismatched entries across three entry paths are tested (18 cases).

   This verifies object mapping, not physical left/right attribution or the
   quality of packed speed measurements. The executor retains the fixture's
   protocol convention subindex 1/low half and subindex 2/high half; this change
   does not establish that correspondence on hardware. The checkpoint's open
   feedback/watchdog work still applies, and no production safety fact is inferred
   from fake vcan responses. Concurrent external drive reconfiguration is outside
   the exclusive qualification-session contract.

3. **Transient receive failures.** EINTR/EAGAIN/EWOULDBLOCK from the nonblocking
   peek now clear the pending epoll dispatch and return to the owner loop without
   consuming or publishing a frame. The next iteration still checks deadlines
   and termination. Fatal errors and short frames remain failures. Linker-wrapped
   syscall tests prove one-shot recovery, single consumption, continued deadline
   and SIGTERM handling under repeated EAGAIN, and retained fatal-error handling.

4. **Shared stderr flags.** The logging port no longer calls F_SETFL on stderr.
   Socket sinks use per-call MSG_DONTWAIT/MSG_NOSIGNAL. Other sinks are reopened
   through /proc/self/fd/2 with an independent nonblocking append description and
   closed after the record. A dup would share the original open-file flags and
   would not solve this problem. Reopen/write/close failures remain visible through
   Logger health; there is no blocking fallback. Non-socket output requires procfs
   and permission to reopen the sink. Regular-file output appends without moving
   the original description's offset. This does not promise hard real-time disk
   I/O. Existing signal-mask lifetime behavior is unchanged.

## Regression evidence

Tests preceded the production changes. The initial four CLI assertions failed
because dangerous operations reached lifecycle creation, the write-boundary
assertion observed changed stderr flags, and injected EINTR/EAGAIN made the
observer fail. After correction:

| Configuration | Result |
| --- | --- |
| GCC 13.3 host Debug qualification | 57/57 |
| LLVM 22.1.8 ASan/UBSan qualification | 57/57 |
| Default Debug / Release | 28/28 each |
| P5.6 commissioning isolation | 36/36 |
| Fixed GCC 11.4 RK3588 qualification Debug | Build + ELF audit PASS |
| Fixed GCC 11.4 RK3588 default Debug / Release | Build + ELF audits PASS |
| Scoped LLVM 22.1.8 clang-tidy | Exit 0; 0 errors; 53 reviewed advisories |

Managed vcan tests actually ran in isolated user/network namespaces. Socket,
pipe, PTY and log tests use local simulated resources. The ordinary socket test
still skips its optional direct vcan section; the separate managed test supplies
that coverage. No test skip is recorded in the retained CTest XML files.

The advisory count includes test files and headers, unlike the older 21-advisory
scope. Categories and disposition are in the new test_result.json. Initial tidy
invocations failed first for no enabled checks, then for missing Ninja .modmap
response files (also with scanning disabled). All failed logs are retained.
A separate Unix Makefiles compilation database with module scanning disabled
allowed the same scoped analysis to finish without deleting compiler arguments
or suppressing diagnostics.

## Reproduction and artifact boundaries

The main configure/build/test commands, from the repository root, are:

```sh
cmake -S . -B out/build/review-fixes -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION=ON \
  -DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON
cmake --build out/build/review-fixes --parallel 4
ctest --test-dir out/build/review-fixes --output-on-failure --output-junit out-review-results.xml
```

For the sanitizer build, use out/build/review-fixes-sanitizer, select the installed
LLVM 22.1.8 clang/clang++ and add -fsanitize=address,undefined to C/C++ flags.
Tests used ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 and
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. Default builds omit the
qualification flag; commissioning instead enables
ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING. All use BUILD_TESTING=ON.

Static analysis used the qualification configuration with generator Unix
Makefiles, CMAKE_EXPORT_COMPILE_COMMANDS=ON, CMAKE_CXX_SCAN_FOR_MODULES=OFF,
and checks -*,clang-analyzer-*,bugprone-*,performance-*,portability-* over the
four changed production translation units and three changed test units.

Cross builds used the verified docker/cross/image.lock image, a read-only source
snapshot and the validated actual-target sysroot with the repository toolchain
file. The container had no network, dropped all capabilities, and wrote only
under out. The local runner is out/build/review-fixes/run-cross.py; source
attestation and ELF hashes are retained below. The snapshot predates only final
CLI whitespace, test-wrapper Doxygen comments and verification documentation.
At this software-verification checkpoint, no target deployment/smoke/HIL or remote
CI had been performed for these artifacts. Subsequent diagnostic staging and
partial HIL results are recorded in [the HIL disposition](P6_REVIEW_HIL.md).

See [retained results and logs](evidence/p6_review_20260910_safety_io/test_result.json).
That directory includes five CTest XML reports, successful and failed tidy logs,
source hashes, cross-source attestation and a checksum manifest. Historical
accepted HIL artifact hashes remain unchanged and do not attest this new binary.
