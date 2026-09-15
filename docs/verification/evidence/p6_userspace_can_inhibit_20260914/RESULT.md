# Userspace CAN inhibitor software result — 2026-09-14

Status: **SOFTWARE PASS; TARGET AND PHYSICAL VALIDATION OPEN**

The drive remained powered off. No target deployment, file-capability change,
physical CAN operation, motor command, kernel build or SDK download occurred.

Implemented and verified:

- fixed-interface helper accepts only `can0` and requires effective
  `CAP_NET_ADMIN`;
- helper removes normal CAN filters and independently receives CAN error frames;
- explicit inhibit, CAN error, malformed command and parent EOF set and verify
  `can0` administratively down;
- explicit release is accepted only through the private inherited channel;
- the helper never sets the interface up and never transmits CAN;
- `--external-loss-once` requires `can0` plus an absolute helper path;
- qualification startup/lifecycle/readiness/operation failure explicitly
  inhibits the interface before returning;
- successful verified cleanup may explicitly release the helper;
- default Debug/Release builds do not produce the helper executable.

Final checks:

- host qualification: 68/68 passed;
- ASan/UBSan qualification: 68/68 passed;
- default Debug: 28/28 passed;
- default Release: 28/28 passed;
- isolated inhibitor test: release, explicit down, parent EOF, independent CAN
  error and qualification-failure down all passed;
- LLVM static analysis, clang-format, shellcheck and whitespace checks passed;
- offline RK3588 GCC 11.4 qualification/default Release cross builds passed;
- qualification ELF, helper ELF and default platform-probe ELF audits passed
  with no RPATH/RUNPATH.

Cross artifact SHA256:

```text
fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4  robot-control-zlac-qualification
c2425001cbe583e3bb1e3307d4145e95cafd722d150154ff727fe519a80b1a5b  robot-control-can-interface-inhibitor
82a5febe43eb2a3f0fa2094605cccf796ba9d4031fcdf051d929026df7b9b228  robot-control-platform-probe
```

The initial expected test-first compile failure is retained in
`red_compile.log`. `build.log` retains the first implementation compile failure
that exposed the include conflict and inherited-fd argument issue; the final
build is recorded in `final_build.log`.

This does not close the physical defect. The helper still invokes the existing
RK3588 CAN driver close path. A separately authorized target and unloaded
dual-capture cable test must prove timely interface inhibition and absence of
post-error frames after reconnection.
