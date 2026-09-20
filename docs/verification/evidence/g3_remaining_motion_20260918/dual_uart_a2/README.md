# G3 dual-wheel UART loss A2 — accepted (2026-09-20)

Artifact `9b336936`, right sign -1, two raised/unloaded wheels, <=5rpm and
<3s. User confirmed readiness in chat and entered fresh START in the local
terminal. The target identity/hash and unused one-shot runner were checked.
Both host/capture markers are consumed; target `physical-once` also prevents reuse.

Dual-forward motion lasted 759.994ms. Signal-only interruption produced a
100ms valid-frame timeout (source fault5), before the automatic cutoff. First
zero was 15.024us after the calculated frame-expiry threshold on the application
monotonic clock; this is not a measured electrical disconnect-to-zero latency.
There were 14 simultaneous directional feedback samples, no RF flags, X1 or
restart. Stable +/-2rpm stop was verified within 410.414ms, including at least
150ms continuous hold and a fresh frame.

1513 dual-capture frames match exactly. All 36 bounded volatile writes and full
baseline restoration pass. CAN counters are unchanged. Three missed periods and
39.873ms maximum lateness are retained; no hard-real-time claim.

The original auditor fails on `feedback_bad=1`; `feedback_hard_bad=1`, exit1
and primary_ok0 are preserved. A single right raw +2.6rpm (vehicle logical
-2.6rpm) sample occurred 110.611ms after zero. Under the previously approved
NORMAL_START_STOP_FEEDBACK_TRANSIENT criterion it is accepted retrospectively:
no active-motion wrong direction, no pre-enable exception, all raw feedback
within +/-7.5rpm, bounded stable stop and full restoration. No raw sample or
online guard was changed. The one-sample check is specific to this evidence,
not a new general transient threshold.

Operator OFF confirms both vehicle-forward directions and stops normal, no
abnormal sound, drive OFF, only signal removed, power/ground/USB unchanged,
transmitter/receiver continuously powered and signal restored after power-off.
Scope: dual-forward Reader-observed UART loss only; no electrical measurement,
RF-loss, recovery/rearm, ground or loaded acceptance. A1 remains incomplete.

Reproduce the original failure with `python3 -B` on `analyze-uart.py`; reproduce
the separate acceptance with:

```sh
python3 -B docs/verification/evidence/g3_remaining_motion_20260918/dual_uart_a2/audit-transient.py
```

The supplemental audit reuses the original UART causal, counter, operator and
protocol/restoration checks; raw feedback is checked separately on application
and wire clocks. Original failure is retained in `original-audit-failure.txt`.
