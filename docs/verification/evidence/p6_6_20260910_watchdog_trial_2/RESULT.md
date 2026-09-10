# Watchdog attempt 2: executor pass, physical acceptance open

After the operator reported readiness following a requested driver power cycle,
the same authorized fixed trial ran once. Setup and cleanup passed; the
qualification executable exited zero. The artifact SHA256 is
a03895cb4bb60a98783e6c705a89ddd91ce7152c5dd337074360a41d176c6c2b.

Both captures contain the same ordered 182 frames: 62 target TX (16 downloads,
44 uploads, two NMT) and 120 captured RX. Exactly one subindex-2 +5 rpm target
was sent. Watchdog raw 1000 was written/read back before that target. No
heartbeat or TPDO suppression trial was run.

The last target upload request was followed by 1700.008 ms with no host TX.
The first subsequent reads returned zero for both independent velocities and
packed velocity before any cleanup zero/controlword/NMT command. First cleanup
zero occurred 5.996 ms after the first probe; Shutdown occurred at 13.937 ms.

Right-axis independent feedback subsequently returned raw signed values -3,
-24 and -24 at 39.297, 94.241 and 149.304 ms after the first probe. Final
all-zero feedback was verified at 205.307 ms, Pre-operational heartbeat at
399.196 ms, and complete volatile restoration at 411.360 ms. Final target
readbacks are both zero; watchdog and heartbeat producer are zero, command
application is one. The operator confirmed that the right wheel stopped during
the silent interval and then restarted, while the left wheel remained
stationary and brake behavior and sound stayed normal. This confirms actual
movement after the stop, rather than only a feedback transient.

No renewed nonzero target or Enable Operation request was sent before the
restart. Its association with resumed host traffic is consistent with the
retained target becoming active again, but the exact triggering frame was not
isolated. This watchdog must not be treated as a latched motion inhibit.
Future recovery handling must establish zero/inhibit before routine reads or
other traffic can reactivate a retained command; that recovery sequence still
needs its own validation.

During silence the raw high status half changed from 0x4427 to 0x1427
536.080 ms after the last host TX. Both decode to Operation Enabled. Packed
velocity remained zero throughout, including commanded motion. Therefore the
status change is not a qualified stopping timestamp and does not establish
watchdog timer units or the exact expiry time.

An evidence discrepancy remains: target RX packet counters advanced by 122,
whereas both captures contain only 120 received frames. RX byte counters
advanced by 875, versus 859 captured bytes. TX counters reconcile exactly.
No CAN errors/drops or error-state increments were recorded. A later read-only
postflight found unchanged counters, no residual executor/capture processes,
and the adapter configuration unchanged. Agreement between captures does not
resolve the two-frame/16-byte accounting difference.

The stop-and-restart behavior is now operator-confirmed. Overall physical
watchdog acceptance remains OPEN for RX counter reconciliation and exact timing.
Heartbeat/TPDO motion trials remain unrun; do not infer qualified backup-watchdog
timing from the raw status flag change or a latched stop from the executor's
zero exit code. No further physical attempt was made automatically.
