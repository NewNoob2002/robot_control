# F4 A2 — INCOMPLETE: startup rejected X1 still active

A1 remains INCOMPLETE: operator confirmed using X1 rather than switching off the
transmitter. User explicitly requested this one retest. Same3652f15b executable,
right-only<=5rpm/<3s, CLI15 and all previous capture/cleanup bounds. Unique staging
and markers; no A1 results or readiness reused. Local console highlights handheld
transmitter OFF and requires both START and typed 关闭遥控器 before invocation.
Receiver stays powered. X1 remains available for emergencies, not the test stimulus.

Offline check_runner.py passes three positive/eight negative causal cases and
readiness/prompt checks. Target identity, no competing writers, staged hashes,
--help and pure control-cycle smoke passed. Full F4 acceptance still requires
analyze-sbus.py, matching captures, restoration and actual operator confirmation.

## Actual result

Startup SDO6041 read detected X1 still active (raw341873760/0x14609460).
Application rejected control_hil_x1_active in62.829ms, cycles0, exit1. Operator
recorded “忘记复位X1了” and confirmed drive OFF. No motion was attempted; generic
terminal direction/stop fields are not motion evidence for this attempt.

Dual captures match; one SDO read only, no SDO writes/NMT/RPDO. No configuration
was changed (restore_ok1 is a no-change cleanup, not a full baseline readback).
Complete two-row trace and unchanged CAN counters. Both markers consumed. See
failure-analysis.json for counts and sbus-audit.log for the rejected F4 oracle.
F4 remains OPEN; no automatic retry. Any new attempt needs a fresh one-shot and
current readiness with X1 physically released and transmitter/receiver linked.
