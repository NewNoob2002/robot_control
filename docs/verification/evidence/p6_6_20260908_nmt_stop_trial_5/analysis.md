# P6.6 NMT Stop final-trial analysis

Result: **PASS** for the first P6.6 bounded NMT Stop slice.

- Artifact SHA-256: `269aaa0152cd1a1d46a7d38dfe23fd214e31fa062479a10addf8ad27f4a5bd20`.
- Executor command: `robot-control-zlac-qualification --interface can0 --nmt-stop-once 2:5 --duration-ms 2000`.
- Executor exit code: 0; completion marker: `qualification_complete node=1 operation=7`.
- RK3588 and JCAN each captured 185 frames. Normalized arbitration ID and payload sequences match exactly; JCAN reports zero dropped frames and no warnings.
- The late-start `0x1017:00` upload received the matching `0x581` response before any motion setup.
- Temporary heartbeat producer `0x1017:00=500 ms` was read back, followed by fresh `0x701#7F` and `0x701#05` heartbeats.
- Axis 1 target stayed zero. Axis 2 target `0x60FF:02=+5 rpm` was acknowledged and read back.
- NMT Stop `000#0201` followed the nonzero write by 2000.879 ms. `0x701#04` followed NMT Stop by 104.323 ms.
- Axis 1 and axis 2 zero writes followed NMT Stop by 106.299 ms and 110.107 ms respectively.
- The first post-stop axis 2 velocity upload returned -4 rpm; the next returned 0 at 715.450 ms after NMT Stop.
- Cleanup reached raw statusword `0x1421` (CiA402 Ready to Switch On), NMT Pre-operational `0x701#7F`, restored `0x200F:00=1`, and restored `0x1017:00=0`. Final restoration completed 861.441 ms after NMT Stop.
- No EMCY, SDO abort, malformed frame, or prohibited CAN frame was present. JCAN cleanup reports no warning, no periodic task, and no cleanup error.
- Operator observation agrees with the command: right wheel counter-clockwise for about 2 seconds, left wheel stationary, no abnormal brake action or sound.

Residual: `can0` reported `ERROR-PASSIVE` before and after this trial while both live error counters were zero and cumulative error counters did not advance. This state was inherited from the earlier unpowered attempt. The complete acknowledged 185-frame exchange passed, but the controller-state inconsistency remains separate evidence to investigate before electrical bus-off or interface-loss qualification.
