# Phase 6 X1 emergency-input and zero-motion soak preparation

Prepared: 2026-09-14. Updated: 2026-09-15.

Status: **X1 behavior passed as composite evidence; soak DEFERRED by the operator on 2026-09-15, NOT PASSED**. V3 failed after 309 s / five passing cycles; V4 was not started. See [the current checkpoint](PHASE6_CHECKPOINT.md) and [soak disposition](P6_SOAK_SESSION_REPAIR.md). Earlier OFF/DOWN statements below describe their dated trials, not the current physical state.

## X1 zero-motion pretest

The fixture emergency-stop switch is connected to X1/INPUT2. The operator reports a normally-open contact: it is open while the button is not pressed, locks when pressed, and returns to a maintained reset state after rotating or pulling the button. The fixture has no mechanical brake. Historical readback recorded `0x2030:03 = 9`, no input inversion, and X1 handling value 0 in the high byte of `0x2026:03`; the prepared test does not write these objects.

The consumed one-shot runner in [p6_emergency_input_20260914](evidence/p6_emergency_input_20260914/) reused the existing Debug-only `--manual-tpdo` operation. Its initial read-only status was `0x14001400`; the operation incorrectly rejected CiA402 Not Ready to Switch On (`0x00`) as enabled, so it stopped before heartbeat changes, NMT, TPDO observation, or an X1 prompt. JCAN serial `207F346D5650` was a silent second observer. The repair now accepts `0x00` with the other reviewed non-enabled states and retains rejection of enabled/fault states.

The new [V2 runner](evidence/p6_emergency_input_v2_20260915/RESULT.md) uses repaired RK3588 ELF SHA-256 `bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7`. Its 1989 target and silent-JCAN frames match exactly; 1206 TPDO1 samples and all SDO speed samples are zero. X1 changed only the low status half, `0x1400→0x9400→0x1400`; the high half remained inactive. The operator accepted no motion or abnormal sound. The false wrapper is retained as an analyzer expectation defect.

The operator held X1 active, reset it, and observed more than five seconds with no motion. Acceptance uses the hardware-observed packed-status contract: low-half bit 15 changed inactive, active, inactive while high-half bit 15 remained inactive. All TPDO and sampled SDO velocities remained zero; no CAN error, EMCY, unexpected request, motion, or abnormal sound occurred. This pretest does not establish moving stop time or stopping distance.

## Moving X1 runner

The consumed [moving result](evidence/p6_emergency_input_moving_20260915/RESULT.md) used the installed repaired ELF and bounded `--target-once 2:5 --duration-ms 3000` operation. The right wheel produced nonzero feedback while the left remained zero. The operator pressed and locked X1; low-half bit 15 activated and the right wheel reached stable zero 99.902 ms later, 861.245 ms before the application's scheduled packed zero target. RK3588 and silent JCAN retained the same 225 frames, with no CAN error or later nonzero speed.

The raw wrapper failed because it waited for the operator to type `ESTOP_LOCKED` after the physical press. Twenty-one X1-active TPDO samples prove the stimulus occurred in time. The operator confirms normal stopping, no abnormal sound or restart, and final safe power-off. X1 remained locked until power-off and was then reset. Powered reset/no-restart behavior is supplied by the accepted V2 zero-target trial. No moving retry is required. This raised-wheel +5 rpm evidence does not establish loaded stopping distance or certified safety performance.

## Overnight soak runner

The [dedicated three-hour runner](evidence/p6_zero_motion_soak_20260915/PREPARED.md) uses [phase6_zero_motion_soak.py](../../scripts/hil/phase6_zero_motion_soak.py), Python standard-library code, existing Linux `ip` and `candump`, and the reviewed qualification ELF. It is staged with authorization disabled. It repeats the existing 60-second `--manual-tpdo` lifecycle, retains a continuous SocketCAN capture, records interface health before and after every cycle, stops on the first failed cycle or increased error/drop counter, and writes one `.tar.gz` evidence file. The same script analyzes either the directory or archive on the following day.

The soak requires raised wheels, X1 locked active, drive power on, `can0` ERROR-ACTIVE at 500 kbit/s, and no competing CAN qualification or traffic-generator process. It accepts only heartbeat writes, reviewed read-only SDO uploads, NMT Operational/Pre-operational, and zero-velocity TPDO feedback with low-half statusword bit 15 active while the high half remains inactive. It never sends a target, RPDO, or controlword.

This is a repeated one-minute lifecycle soak. It validates repeated setup, observation, and cleanup, not one uninterrupted application process, motion endurance, load behavior, or real-time performance. After completion or failure, X1 must remain locked until the operator powers the drive off and verifies both wheels stopped.

Local validation:

```text
python3 -m py_compile docs/verification/evidence/p6_emergency_input_moving_20260915/*.py scripts/hil/phase6_zero_motion_soak.py
python3 scripts/test/test_phase6_emergency_input.py
PASS: moving X1 stop, isolation, reset, request, and error checks
python3 scripts/test/test_phase6_zero_motion_soak.py
PASS: Phase 6 zero-motion soak parsers and fail-closed checks
```
