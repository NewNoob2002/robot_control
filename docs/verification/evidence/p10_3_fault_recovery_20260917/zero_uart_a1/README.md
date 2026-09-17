# F5 zero-UART A1 — INCOMPLETE (2026-09-17)

User authorized F5 and confirmed an independently removable SBUS signal port.
Local START confirmed signal-only operation, USB/power/common-ground unchanged,
transmitter ON, unloaded/raised wheels and emergency-stop availability. Reused
cd8bd1c5/CLI20,zero-target60s runner; no firmware change. Fresh target identity,
hashes/help/pure-cycle smoke and offline runner/silence oracle checks passed.

Trial exited on source_fault9 (Reader discontinuity), not100ms valid-frame timeout.
The last nonempty batch had42 raw bytes/one complete frame. After59.970109ms with
no new bytes, buffered partial data caused partial_timeout3 and session2→3.
Source immediately revoked; RecoveryTrial only accepts RF flags/valid-frame timeout
and rejected this earlier discontinuity in phase1. This is safe protective behavior
but the observer does not cover an arbitrary physical unplug boundary. No USB
transport error was recorded. Do not require the operator to time a frame boundary.

2276 dual-capture frames match,1339 RPDOs all zero,36 volatile writes/full baseline
restoration.6853 trace records,1900 valid SBUS frames,one discontinuity,no feedback
violation,CAN counters unchanged. No release_fault/recovery sequence completed.
Operator OFF confirms stationary wheels/no abnormal sound/drive OFF. Free-text
operation response is only “是的”; signal reconnection is not separately confirmed.
Both one-shot markers consumed; no automatic retry. No F5 acceptance is claimed.
See analysis.json,audit-result.py,incomplete-analysis.json and original logs.

A future observer should distinguish an expected signal-boundary partial timeout
from service-gap/backlog/transport errors, preserve immediate revocation and raw
session evidence, then evaluate sustained silence and fresh recovery authorization.
It must not blanket-ignore discontinuities or reclassify this failed run as passing.
