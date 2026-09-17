# F2 zero-SBUS A1 — PASS for observed valid-frame timeout recovery

User authorized F2; unchanged616e8ad3 deployed to an independent target stage, identity,
SHA256/help/pure ControlCycle smoke verified. Local START then one bounded invocation.
Both markers consumed. Operator local OFF confirms wheels stationary/no sound/power OFF.

Actual fault is valid-frame timeout, not flags-based failsafe:6648 valid frames all have
flags0; maximum valid-frame gap10660.005313ms. Fault observed100.019841ms after the last
valid frame. Reader batches inside that gap contain51 raw bytes and no frame events;
therefore this is not proof of electrically silent UART or F5 acceptance.

Independent oracle verifies fault withdrawal, nonneutral CH6 rejection, neutral hold,
fresh source/system1→2 rearm, QuickStopActive→Disable Voltage→fresh Disabled→Shutdown→
Ready→SwitchOn→Enable, and1.010s zero-enabled hold. First rearm completes without a later
CH6 cycle substituting for it. Live console reaches ZERO_REENABLED then STOP/cleanup.

8586 target/JCAN frames match exactly;5721 RPDOs all zero;36 volatile writes and complete
baseline restoration. Complete26675-record trace, no overflow/feedback_bad/discontinuity.
Both speed feedback ranges0rpm; configured±1rpm tolerance. Standstill holds162.009/161.004ms.
CAN errors/drop deltas0 and extended counters unchanged. Application exits0 in58.813s.
Timing retained:3 missed periods, maximum lateness36.149ms, cycle519us, shutdown25us.
No hard-real-time claim. See raw captures, recovery-analysis.json, metrics-and-counters.json
and operator-post-trial.json. Reproduce with python3 -B analyze-recovery.py.

F2 closes only the observed timeout recovery scenario. Flags4/12 failsafe, moving X1,
moving SBUS fault and moving SIGTERM remain unaccepted. Do not classify this transmitter
OFF/ON trial as a separate controlled UART-silence experiment. P10.3 remains OPEN.
A5 files match preserved-a5.json. No automatic retry or next physical test.
