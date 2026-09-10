# Watchdog re-test 3: stationary observation; dynamic result inconclusive

The operator explicitly requested this repeat after the previous stop/restart
observation. The same artifact and physical limits were used once: subindex 2
at +5 rpm, watchdog raw 1000, 200 ms lead and a further 1500 ms quiet window.
The wrapper added four read-only kernel counter snapshots. It did not change
the drive stimulus or transmit additional CAN frames.

RK3588 and independent silent JCAN captures agree on 166 ordered frames:
56 TX (16 downloads, 38 uploads, two NMT) and 110 captured RX. The measured
host-silent gap was 1700.005 ms. Both independent velocity readbacks and packed
velocity were zero before cleanup. All sampled cleanup velocities remained
zero; final all-zero feedback was verified 40.305 ms after the first probe and
volatile restoration completed at 162.344 ms. Exactly one nonzero target and
no renewed Enable Operation request occurred. The executor exited zero.

The RX discrepancy reproduced: kernel counters advanced by 112 packets while
both captures contain 110 received frames. Counters were unchanged between
preflight, capture readiness and immediately before the executor. They were
also unchanged between executor exit, capture-stop readiness and postflight.
This places the discrepancy during executor operation; capture startup/shutdown
boundary movement does not explain it in this run. TX counters reconcile.
All recorded CAN error/drop/error-state counters remain zero.

The raw high status half changed from 0x4427 to 0x1427 at 535.665 ms after the
last host TX, close to the previous trial's 536.080 ms. This remains a raw flag
observation, not a qualified stop timestamp or proof of watchdog timer units.

Final readbacks restore both targets to zero, watchdog and heartbeat producer
to zero, and command application to one. The adapter configuration is unchanged.
Both capture processes and the executor exited cleanly. The operator reported
both wheels stayed stationary, with normal brake behavior and sound. This run
does not demonstrate motion followed by watchdog stopping. The executor's zero
exit only establishes its existing software checks: target readback was accepted,
but independent nonzero velocity was not required before silence. That acceptance
gap is being corrected. The prior operator-confirmed restart remains valid
evidence for attempt 2 and must not be overwritten by this stationary repeat.

The requested re-test is complete, but full physical qualification remains
INCONCLUSIVE for dynamic stopping, with RX reconciliation and exact timing also open. No
heartbeat/TPDO suppression test or further automatic retry occurred.
