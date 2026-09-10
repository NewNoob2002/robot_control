# P6.6 Disable Voltage corrected trial analysis

Result: **HIL PASS**.

- The corrected RK3588 executor returned zero and reported `qualification_complete node=1 operation=9`.
- RK3588 and persistent Rust JCAN 1.0 silent capture each recorded 393 frames. Their normalized arbitration-ID, DLC, and payload sequences match exactly; all JCAN frames were standard Classical CAN data frames.
- The sole channel-2/right target request was `0x60FF:02=+5 rpm`. The sole Disable Voltage request was standard ID `0x601`, DLC 8, payload `2B40600000000000`, sent after a 10001.031 ms nonzero interval.
- The Disable Voltage SDO acknowledgement arrived in 0.307 ms. The first TPDO1 containing raw dual `0x1460` arrived in 31.754 ms and was accepted as dual Switch On Disabled while preserving the raw status.
- Channel 1 remained at measured zero. Channel 2 decayed through 34, -6, 1, and -1 rpm samples and first read zero 420.230 ms after Disable Voltage. Packed velocity was zero by 422.227 ms.
- Cleanup wrote and verified both targets zero, reverified all velocity views at zero, entered NMT Pre-operational at 436.796 ms, observed the Pre-operational heartbeat at 588.977 ms, restored `0x200F:00=1`, and restored `0x1017:00=0`.
- No post-Disable Voltage Shutdown, re-enable, retry, SDO abort, EMCY, CAN error frame, JCAN error, residual executor, or residual candump process occurred. JCAN shut down cleanly and its postflight configuration exactly matches baseline. RK3588 `can0` remained UP and ERROR-ACTIVE at 500000 bit/s with zero error counters and drops.
- The executor log includes the existing gate-denied CANopen startup send and associated epoll messages. No corresponding prohibited physical frame appears in either agreeing raw capture, and the bounded operation completed successfully.

The operator confirmed counter-clockwise right-wheel rotation during the positive channel-2 command, a stationary left wheel, both wheels stopping after Disable Voltage, and no abnormal brake action or sound. This completes the P6.6 Disable Voltage slice.
