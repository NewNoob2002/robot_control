# G3 dual-positive A1 — single bounded raised-wheel trial

2026-09-18. User requested the next test after both negative single-wheel items
were accepted. Artifact5f44146c remains unchanged;123 compiled inputs and binary
identity match. Machine6923ab3301fb4a8d816759b04ec6bf0a, node1/can0 at500k,
UART586D017868, JCAN207F346D5650 receive with hardware ACK/no data sends.

Exact CLI and one-shot scope: authorization.json. Both positive targets equal,
1..5rpm each (Source deadband means normal nonzero3..5rpm);60s total preparation,
3s hard nonzero/2950ms automatic cutoff. No fault injection/ground/load.

Local START confirms actual drive power, raised/unloaded wheels, X1
released/accessible, unchanged wiring, neutral sticks and CH6 released.
CONTROL_READY only after healthy neutral input; press/release CH6 once.
MOTION_READY follows20 Enabled zero cycles: throttle forward only, steering
neutral (CH1=1000/CH3=1800 ->5/5rpm). Hold until STOP or actual wheel stop,
then neutral; after cleanup X1 locked and drive OFF.808 ideal forward throttle
points satisfy equal targets; manual steering deviations can protect/terminate.
Both wheels should propel the vehicle forward; external visual spin directions
are not assumed identical. Operator independently confirms both axes and stop.

Require two simultaneous fresh positive feedback samples for both axes,
absolute feedback<=7.5rpm; exact targets, no restart. Within1s after zero,
both axes must hold+/-2rpm for150ms plus a new frame.36 allowed volatile writes,
complete baseline readback/restoration and globally identical dual capture.
The already user-approved short startup/stop transient classification applies;
record phase/count/peak/timing, never discard raw values or skip stable stop.
Current online guard is unchanged and can return its historical feedback failure;
that raw outcome and post-trial acceptance remain separate. No unlimited
frame-ignore window or automatic retry. Sustained wrong direction/overspeed,
invalid targets, timing/restore/capture failures remain failures.

Host lease3s and capture-health supervision revoke to bounded SIGTERM cleanup;
local emergency stop/power OFF remains available. All failures preserved,
one-shot consumed, no automatic retry. Startup requires this trial's local START.
