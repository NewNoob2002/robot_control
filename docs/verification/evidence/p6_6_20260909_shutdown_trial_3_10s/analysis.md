# P6.6 Shutdown 10-second trial analysis

Result: **PASS**.

- The corrected RK3588 executor returned zero and reported `qualification_complete node=1 operation=8`.
- RK3588 and the persistent JCAN 1.0 silent session each captured 381 frames. Their normalized arbitration-ID and payload sequences match exactly.
- Channel 2 held `0x60FF:02=+5 rpm` for 10000.919 ms before the sole final `0x6040:00=0x0006` Shutdown request.
- The Shutdown SDO acknowledgement followed in 0.322 ms. TPDO status reached raw `0x1421` (both axes Ready to Switch On) in 31.564 ms.
- Channel-2 measured velocity samples after Shutdown were `+21, -8, +2, +1, 0, -1, 0 rpm`. The first measured zero arrived at 254.307 ms and cleanup verified zero again at 323.279 ms.
- Cleanup wrote axis-1 and axis-2 targets zero at 257.981 ms and 261.983 ms, entered NMT Pre-operational at 326.867 ms, received `0x701#7F`, restored `0x200F:00=1`, and restored `0x1017:00=0` by 343.972 ms.
- No EMCY, CAN error frame, SDO abort, malformed frame, or prohibited frame occurred. RK3588 `can0` remained ERROR-ACTIVE at 500000 bit/s with zero error counters and drops.
- JCAN shut down cleanly, reported no operation error, and its raw postflight configuration matches the preflight baseline. No qualification executor remains.

At 2026-09-09T09:32:47Z, the operator confirmed counter-clockwise right-wheel rotation, a stationary left wheel, and no abnormal brake action or sound. This completes the P6.6 Shutdown slice.
