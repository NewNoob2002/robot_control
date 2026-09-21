> Historical H1 checkpoint. The subsequent SBUS-only app removes the deferred IPC configuration keys; use docs/development/TEST_APP.md and the current config file. H1 artifact hashes below are unchanged historical evidence.

# Test-app H1 startup contract — 2026-09-20

Started after baseline0f07dbc and checkpoint/ground-functions-20260920 were pushed.
That baseline's GitHub Actions run35495981865 completed successfully. The H1
changes are subsequent local work and are not covered by that remote result.

## Implemented

Default-OFF ROBOT_CONTROL_BUILD_HANDOFF_TOOLS builds robot-control-config-check.
The owned Startup value contains typed CycleConfig/SourceConfig and explicit
endpoint/log/run-duration fields. Existing domain validators are reused; the
physical RK3588/ZLAC profile is versioned and fixed to the accepted calibration
and polarity. No safety, arbitration, HIL gate or device-lifecycle implementation
is modified. The checker does not create device/socket/log endpoints.

Strict bounded key=value parsing supports one CLI override per key; unknown and
duplicate fields, integer overflow/suffixes, invalid endpoints and unsupported
profiles fail. Effective settings round-trip through the same parser.
The template device is deliberately a placeholder, not a detected device.

## Validation

- Debug and Release startup_config_contract pass; the subprocess matrix covers
  range/size/format boundaries, CRLF/no-final-newline, overrides, invalid CLI,
  round trip and no output endpoint creation.
- Clang22.1.8 ASan/UBSan startup_config_contract passes; detect_leaks=0 locally.
- Complete default Debug build passes. Broader applicable host selection:
  31 passed,1 vcan test skipped by unavailable namespace facilities,4 excluded
  SocketCAN/Linux-specific tests. This is not a full vcan validation claim.
- clang-format/clang-tidy22.1.8 passes for both new translation units; only
  suppressed non-user-code warnings. actionlint, CI routing and diff checks pass.
- Locked offline Docker/real RK3588 Ubuntu22.04 sysroot cross build passes.
  Existing audit_qualification_elf.sh verifies aarch64 loader, dependency presence,
  GLIBC/GLIBCXX/CXXABI symbol versions and no RPATH. All7 new/changed build/code/
  config inputs match the frozen cross snapshot. No target execution occurred.
- The optional checker/test is added to future full host CI. This workflow change
  has local actionlint validation, not a new remote CI result yet.

## Artifacts and reproduction

Local evidence: out/handoff/h1/{build,host,sanitizer,release,static,cross-escalated,elf}.log
and cross-metadata.json. Original sandbox Docker denial is retained in cross.log;
escalated build used the existing locked image without changing its lock.

RK3588 checker SHA256:
e8e7d0fb7f97bd421c590a75b8710f4765df461f8c982d05f3a814b7894da5c6.
Local staging: out/handoff/h1/rk3588/ with configuration and SHA256SUMS.

```sh
cmake -S . -B out/build/handoff-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_HANDOFF_TOOLS=ON
cmake --build out/build/handoff-debug --parallel 2
ctest --test-dir out/build/handoff-debug --output-on-failure -R startup_config_contract
out/build/handoff-debug/app/robot-control-config-check \
  --config config/rk3588-test.conf --set control_source=ipc
```

H2 IPC, H3 real app, H4 continuous logging/package and H5 fresh target regression
remain pending. Configuration validity is not hardware readiness or authorization.
