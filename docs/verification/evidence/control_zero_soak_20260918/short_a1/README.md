# Short A1 — FAILED during capture startup, consumed

candump rejected -L together with -x on the target. The supervisor noticed its
exit and sent SIGTERM to the control process during initial SDO reads.
Trace records14 successful SDO read requests, no writes, no RPDO, zero control
cycles and zero enabled samples. Cleanup reports success. Operator confirmed
stationary/no abnormal sound/drive OFF through the local terminal OFF response.
Original application/collector/target results remain in target-result.tar.gz;
host result, JCAN capture and consumed markers are unchanged. No one-hour
attempt started. This failure is not CAN/JCAN framing recurrence or a soak pass.

Repair reuses the existing Phase6 capture helper and checks capture survival for
500ms before starting the application. A failing-capture inert test reproduces
the old premature start and passes after correction. Target receive-only1s
capture smoke is separate. A2 needs fresh local readiness; A1 is never retried.
