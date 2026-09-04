# Periodic 0x7FF target receive control

Date: 2026-09-04

The operator independently ran `candump -ta can0` on the RK3588 while JCAN
transmitted standard ID `0x7FF`, DLC 8, payload
`01 02 03 04 05 06 07 08` at a 50 ms period. The supplied capture excerpt
shows continuous correctly decoded frames spaced at approximately 50 ms.

The agent then performed a read-only postflight. Relative to the earlier
send-once state, target counters had advanced from RXF=7/RXMF=0 to
RXF=1207/RXMF=393. Maximum receive rate was 20 frames/s, exactly matching the
50 ms stimulus. `can0` remained ERROR-ACTIVE with TX=0 and zero error counters.

Result: PASS as a general RK3588 SocketCAN raw-receive control. This evidence
supersedes any claim that `rockchip_canfd` cannot generally deliver physical
CAN data frames to `candump`. It does not close the P5.7 CANopen gate because
ID `0x7FF` is neither the node-1 SDO response `0x581` nor another
drive-originated CANopen frame accepted by the normal observer.

The unresolved difference is now limited to the JCAN single-shot/SDO stimulus
case: physical one-shot transmission confirmation, ID `0x601` behavior, or a
possible CAN error-frame classification. A future active diagnostic must be
separately authorized and include an error-enabled capture such as
`candump -ta -e can0`.
