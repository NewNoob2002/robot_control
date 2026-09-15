# X1 zero-motion pretest attempt 1 result

Date: 2026-09-15

Classification: **STARTUP PRECONDITION FALSE REJECTION; X1 STIMULUS NOT EXECUTED; RUNNER CONSUMED**.

The target qualification process sent one read-only `0x6041:00` upload and received packed status `0x14001400`. No heartbeat write, NMT command, RPDO, target, controlword, EMCY, or CAN error frame was captured. The operator-action marker was never created, so the runner never displayed the X1 action prompt. Both the qualification process and both captures stopped.

`0x1400 & 0x006f` is `0x0000` for each status half. The shared CiA402 decoder already classifies this as Not Ready to Switch On, but `capture_manual_tpdo()` accepted only masks `0x21`, `0x40`, and `0x60`. It therefore reported `qualification_manual_drive_enabled` for a non-enabled state.

The repair adds `0x00` to the accepted non-enabled states and adds a managed-vcan scenario that uses `0x14001400` for the initial SDO status, TPDO stream, and final status. Operation Enabled and the other actuating or fault states remain rejected. Host and ASan/UBSan suites pass 68/68; default host tests pass 28/28; the focused managed-vcan run passes with zero prohibited frames; and the RK3588 cross ELF builds and passes ABI/dependency audit.

The first physical authorization is consumed. No retry is authorized by this result. The operator subsequently confirmed drive power OFF, stopped/safe wheels, no X1 action, and the powered-off finalizer left `can0` DOWN.
