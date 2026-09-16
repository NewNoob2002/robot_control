# P9.1 — Pure SBUS parser verification

Date: 2026-09-16. **P9.1 PASS — host, sanitizer, scoped static checks and the restored
locked-container full aarch64 Debug build.** No target UART, physical CAN, or motion was exercised.
This is not a P9.2/P9.3 input-health or receiver qualification result.

## Implementation

- [Parser](../../input/sbus/protocol/parser.hpp) supports the fixed P9.0 profile:
  25 bytes, header 0x0f, footer 0x00, 16 raw 11-bit channels and all raw flag bits.
- One consume call returns at most one complete/rejected event and its consumed
  prefix. The caller drains the remaining span, preserving lost/failsafe events
  before subsequent healthy frames in the same read. Empty input makes no event.
- The candidate buffer is a fixed 25-byte array; rejected candidates retain the
  earliest subsequent header suffix. No allocation, Linux API, clock, CAN access,
  channel mapping, or motion authority is present. Reset discards partial state
  and diagnostics; the later source must independently invalidate health.
- Diagnostic counters are unsigned modulo-2^64 observations, never safety gates.
  Header/footer framing alone cannot detect every corruption or dropped byte.
- The library is a separate CMake target and the existing CTest suite registers
  one Release-safe assertion executable, [sbus_parser_tests.cpp](../../tests/unit/sbus_parser_tests.cpp).

## Contract coverage

Tests were written before parser implementation; the test translation unit
passed a syntax-only compile against the declared API before parser definitions.

| Contract | Executed checks |
| --- | --- |
| V01/V02 | All-zero and all-2047 channels, digital and loss flags |
| V03 | All 176 independent single-bit positions and a literal mixed-channel legacy vector |
| V04 | Every split 1–24, chunk sizes 1–50 over two frames, empty chunks, exact consumption |
| V05 | Bad footer, truncation with all 24 retained-header offsets, embedded headers, earliest of multiple candidate headers, reset isolation |
| V06 | Every flag bit independently, raw upper-bit preservation, ordered lost/failsafe/healthy frames |
| Bounds | 1 MiB of noise, repeated false headers, fixed parser object size, buffered length below 25 after every call |

No round-trip encoder is used as the expected-value oracle. Health invalidation
after flagged frames remains P9.3; these tests prove event delivery, not recovery.

## Host verification

| Gate | Result |
| --- | --- |
| Narrow sbus_parser_contract | PASS |
| Restored Ubuntu GCC 11.4 host narrow test | 1/1 PASS in out/build/p9-gcc11-host |
| Fresh GCC 16.2.1 Debug build and CTest | 29/29 PASS, no skips |
| Fresh GCC 16.2.1 Release build and CTest | 29/29 PASS, no skips |
| Fresh Clang 22.1.8 ASan/UBSan build and CTest | 29/29 PASS, no skips; detect_leaks=0 |
| clang-format 22.1.8, changed C++ files | PASS |
| Scoped clang-tidy analyzer/bugprone/performance/portability | PASS with project-style pragma-once exception below |
| Dependency verification | Pinned CANopen pair clean, nested gitlink PASS |

Commands run from the repository root:

```bash
cmake --preset host-test -B out/build/p9-host-debug
cmake --build out/build/p9-host-debug -j 4
ctest --test-dir out/build/p9-host-debug --output-on-failure
cmake --preset host-release -B out/build/p9-host-release
cmake --build out/build/p9-host-release -j 4
ctest --test-dir out/build/p9-host-release --output-on-failure
cmake --preset host-test -B out/build/p9-sanitizer \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build out/build/p9-sanitizer -j 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir out/build/p9-sanitizer --output-on-failure
clang-format --dry-run --Werror input/sbus/protocol/parser.hpp \
  input/sbus/protocol/parser.cpp tests/unit/sbus_parser_tests.cpp
clang-tidy -p out/build/p9-host-debug input/sbus/protocol/parser.cpp \
  tests/unit/sbus_parser_tests.cpp \
  '-checks=-*,clang-analyzer-*,bugprone-*,performance-*,portability-*,-portability-avoid-pragma-once' \
  '-warnings-as-errors=*'
```

CTest logs remain in each build directory's Testing/Temporary/LastTest.log.
The first reuse of old host-test/host-release output directories inside the
sandbox had SocketCAN permission failures, managed-vcan skips and logging test
failures. Fresh isolated builds and the authorized outside-sandbox host tests
passed all checks without source changes to those modules. No root-cause claim
is made for the stale-directory logging results.

The initial bare clang-tidy invocation enabled no checks. The existing project's
explicit check families were then selected; Clang 22's pragma-once portability
warning was excluded to retain the repository's existing header convention.
No correctness warning was suppressed; remaining messages were in system headers.

## Earlier local-compiler investigation (retained limits)

The operator requested checking the installed host cross compiler instead of
restoring Docker. It is aarch64-linux-gnu GCC 16.1.0 / binutils 2.47, not the
locked Ubuntu GCC 11.4 container. At that point the Docker image listing was empty and locked-image verification failed. The later authorized restoration is recorded below; this failed local GCC16 attempt is not relabeled as passed.

The real Ubuntu 22.04 sysroot initially failed manifest verification. Comparing
all 26700 records established exactly 65 directory mode differences, 0775 in
the manifest versus 0755 on disk. Only those modes were restored. All file hashes,
symlink targets and manifests remained unchanged. The original external lock
then validated successfully:

```bash
./scripts/sysroot/validate_sysroot.sh \
  sysroots/rk3588-ubuntu2204 sysroots/rk3588-ubuntu2204.lock.json
```

The installed compiler's default C/C++ include and runtime paths remain outside
the sysroot even when --sysroot is given. The experimental CMake build therefore
uses -nostdinc, explicit target C++11/multiarch/C headers plus the compiler's
builtin headers, and explicit target startup/library search paths. It reuses
the existing rk3588-debug preset/toolchain in out/build/p9-host-cross; it does
not alter the production toolchain or use the compiler filesystem as a sysroot.

| Cross check | Result |
| --- | --- |
| robot_control_input_sbus_protocol | PASS: AArch64 relocatable object, no undefined symbols |
| robot-control-platform-probe | PASS: target libraries linked; existing audit_elf.sh validates interpreter, GLIBC/GLIBCXX/CXXABI versions, required platform symbols and no RPATH |
| Entire default cross build | FAIL: existing domain/drive/zlac8015d.cpp instantiates target C++11 bit_cast; GCC16 rejects builtin traits in that header's function signature |
| Locked container at this earlier attempt | NOT RUN: image absent then; see subsequent restoration |
| Target execution | NOT RUN: no target access in P9.1 |

Successful scoped commands:

```bash
cmake --build out/build/p9-host-cross --target \
  robot_control_input_sbus_protocol tools/platform_probe/robot-control-platform-probe -j 4
./scripts/build/audit_elf.sh \
  out/build/p9-host-cross/tools/platform_probe/robot-control-platform-probe \
  sysroots/rk3588-ubuntu2204
aarch64-linux-gnu-readelf -h \
  out/build/p9-host-cross/input/sbus/protocol/CMakeFiles/robot_control_input_sbus_protocol.dir/parser.cpp.o
aarch64-linux-gnu-nm -u \
  out/build/p9-host-cross/input/sbus/protocol/librobot_control_input_sbus_protocol.a
```

These results prove the parser compiles for AArch64 with target headers; they do
not certify the whole repository with GCC16 or replace the locked toolchain.
Do not modify vendor headers, relax warnings or change existing drive semantics
just to label the full cross build passed. The following locked-container
restoration closes that gate without accepting the incompatible local compiler.

## Restored locked-container gate — PASS

The operator authorized restoring Docker. Buildx v0.37.1 was installed as a
user plugin after verifying official SHA-256
`9447199cdb435f25880548343c128a4b6650e8891ee598905d8d29d39a8e359b`.
Daemon and build-client proxy paths were configured separately. The cross-image
script forwards proxy build arguments by name and permits an explicit build
network; its negative-path tests and ShellCheck passed.

The original Ubuntu base digest, APT snapshot, complete packages.lock, Dockerfile
and tool versions remain unchanged. The rebuilt image identity is recorded
in docker/cross/image.lock; candidate and final verification both passed:

- Image: `rk3588-cross:phase1-20260814`.
- ID: `sha256:9c7124ca8b89ac46c7e196df46b260ec8986503eb525739823ac1b719ef92935`.
- Compiler: Ubuntu aarch64 GCC 11.4.0-1ubuntu1~22.04.3; CMake 3.22.1; Ninja 1.10.1.
- Real Ubuntu 22.04 target sysroot and original external lock validated before
  and after the build, mounted read-only throughout.

Executed from the repository root:

```bash
ROBOT_CONTROL_SYSROOT="$PWD/sysroots/rk3588-ubuntu2204" \
ROBOT_CONTROL_SYSROOT_LOCK="$PWD/sysroots/rk3588-ubuntu2204.lock.json" \
ROBOT_CONTROL_PRESET=rk3588-debug ./scripts/build/build_rk3588.sh
```

All 49 build steps passed, including the parser and unchanged bit_cast consumer
that failed under host GCC16. The platform-probe ELF audit passed interpreter,
library availability, GLIBC/GLIBCXX/CXXABI, required symbols and no-RPATH checks.
Build metadata is in `out/artifacts/rk3588-debug/build-metadata.json`; this
working-tree build records a dirty source snapshot, not a clean release build.

Verified source/artifact SHA-256:

```text
c191172257af325e1c01088d164b9035e6fce072edd2ec18848615b8d336e3aa  input/sbus/protocol/parser.hpp
dcf867552656a3670035d8a459b4eec36751307dd03efb5455349a6e79fe6845  input/sbus/protocol/parser.cpp
0745aef434d3d3ed554f7734c8ed1b936b56c21f4cabc80f51f74163dcfa306d  tests/unit/sbus_parser_tests.cpp
1d06fab3032eb5435193d886cf29524a5c382091226106ddb9ece35970d83f51  cross Debug parser static library
d7d522f8ac27fbee32887db05b0514cb4c29d87acf8fa0fedea25ecb61fda36b  cross Debug platform probe
```

P9.1's software-only scope is accepted. Physical receiver/UART checks remain
P9.2; health, mapping and CommandSample production remain P9.3. No hardware
test is claimed, and Phase 6 remains open. Remote CI is reported after push,
not predeclared here.
