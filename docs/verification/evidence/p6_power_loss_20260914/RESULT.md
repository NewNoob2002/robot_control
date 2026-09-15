# P6 drive-only power-loss attempt 1 — 2026-09-14

Disposition: **INVALID STIMULUS TIMING; CAPTURE BOUND EXCEEDED; SAFE FINAL STATE**.

This consumed attempt does not qualify drive-power-loss stopping. The application
sent its first post-motion packed zero 7998.110 ms after the nonzero target and
then inhibited `can0` after the absolute external-loss deadline. The operator's
`POWER_OFF` marker followed interface-down by 1010.868 ms and packed zero by
1934.803 ms. Drive power therefore did not trigger the observed stop.

The target captured 593 frames: one right `+5 rpm` packed target, 175 TPDO1
frames, one controller error and zero RK3588 requests after that error. The
interface went DOWN 15.978 ms after the error. The executor returned nonzero
with `qualification_expected_external_loss_absent`; its cleanup was not
verified.

The operator then completed one drive power-off, held it off for 10.760 seconds,
restored power, entered `NO_RESTART` after a 22.101-second observation interval,
and entered `FINAL_POWER_OFF`. Silent JCAN observed the post-motion drive boot
and no later host request, but retained 161774 frames, including 161218 repeated
`0x701` boot heartbeats while no active node acknowledged the drive. The
coordinator stopped when its 100000-frame validator bound was exceeded. JCAN
configuration remained unchanged.

The target wrapper's `final_drive_power_off=false` was written when the
coordinator terminated the remote process before the final operator marker. The
later retained `operator_final_power_off.json` records the completed final
power-off. Current read-only postflight shows `can0` DOWN/STOPPED and no residual
qualification or capture process.

See `analysis.json`, both raw captures, the four operator markers and
`coordinator_result.json`. This attempt is consumed and must not be retried.
The separately prepared v2 uses JCAN `normal --receive` to provide link-layer
ACK without submitting data-frame send commands.
