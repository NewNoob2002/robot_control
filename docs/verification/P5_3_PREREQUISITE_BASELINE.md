# P5.3 Receive-Only Prerequisite Baseline

Evidence date: 2026-08-25. Result: **PASS; Release cross deferred**.

Commit `f78a74d8ac28271319e6b1aefaae75d71e082e3e` closes the two
prerequisites identified by the P5.2 independent review. It does not implement
the P5.3 socket lifecycle or event loop.

## Implemented contracts

- The pinned Linux driver compiles with its `send()` calls replaced by the
  project-owned `robot_control_canopen_deny_transmit()`. The function performs
  no syscall, always returns `-1`, and sets `errno` to `EACCES`. No transmit
  authorization path exists in this slice.
- `StackStorage` clears every real generated `OD_entry.extension` before
  resetting the dictionary and before `CO_delete()`, while the process-wide
  owner claim is held. This covers later partial-init failure, teardown, and
  reacquisition through the existing owner cleanup path.

## Red/green host evidence

Before implementation, `canopen_stack_build` failed four assertions: two stale
OD-extension checks and two transmit-boundary checks for direct `CO_CANsend()`
and the pinned initial `CO_NMT_process()` boot-up path.

After implementation, fresh Debug and Release builds each completed 50/50
steps. Both commands below exited 0 with 18/18 passed and no skipped tests:

```bash
rtk ctest --test-dir /tmp/robot-control-p53-prereq-debug \
  --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$' \
  --output-on-failure
rtk ctest --test-dir /tmp/robot-control-p53-prereq-release \
  --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$' \
  --output-on-failure
```

The transmit contract uses a valid `/dev/null` descriptor, not a network or CAN
socket. `EACCES` proves the project gate ran before the Linux `send()` syscall.
The NMT state still advances from initializing to pre-operational, so observer
processing is not coupled to successful transmission.

## Source and binary audit

Debug and Release compile databases show project warnings and `-Werror` on
`transmit_gate.c`, and this source-specific definition on pinned `CO_driver.c`:

```text
-Dsend=robot_control_canopen_deny_transmit
```

The linked host test executables have no undefined `send` symbol. `nm` and
disassembly show `CO_driver.c` resolving
`robot_control_canopen_deny_transmit`, and `CO_CANsend()` calling that function
at the former syscall site. All selected CANopen modules continue to submit
through `CO_CANsend()`.

## RK3588 cross evidence

The network-disabled RK3588 Debug cross build exited 0, completed 39/39 steps,
and passed interpreter, dependency, symbol-version, Phase 3 symbol, and RPATH
audits. Its compile database contains the same send replacement and applies the
project warning policy to the new C source.

The Debug metadata records revision `f78a74d8ac28271319e6b1aefaae75d71e082e3e`,
`dirty: true`, and snapshot SHA-256
`e845d20e8166459e23990c4c6953a356f98917c5206a408f5c219be05fa7b313`
because unrelated user-staged files remained present and untouched. The Debug
platform-probe SHA-256 is
`60467297cc86e281af7a48774c95bb4c8d7f4819e8afa63def1f557ae23b65e6`.

The RK3588 Release command exited 3 at the expected clean-source gate:

```text
rk3588-release requires a clean source snapshot
```

Release cross qualification must be rerun when the unrelated staged worktree
changes have been resolved. No file was unstaged, reverted, or included in the
CANopen fix commit to bypass this gate.

No CAN, `vcan`, SocketCAN runtime, physical CAN, RK3588 access, deployment,
driver/network configuration, or motion operation was performed.
