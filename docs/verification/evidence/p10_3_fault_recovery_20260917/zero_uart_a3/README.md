# F5 UART A3 — INCOMPLETE (2026-09-17)

Explicit user 启动A3 authorization; bcd4cfe9/CLI20,120s total,5s minimum silence
hold,45s reconnection/challenge. Target identity/hash/help/pure-cycle smoke and
fresh START passed. One invocation, both markers consumed; no automatic retry.

Reader raw-byte silence16350.004599ms verified. One expected partial-timeout
boundary/session2→3 withdrew authority; required hold5009.994396ms completed.
Initial withdrawal was partial timeout, not the100ms valid-frame timeout.
11.300s after RELEASE_FAULT, fresh healthy Disabled input produced INPUT_RESTORED.
470.023ms later two new rejected parser candidates occurred; phase2/source_fault2
triggered protected exit before nonneutral/rearm completion. Recorded axes stayed
neutral and authority revoked. This is not a short-window failure; exact physical
cause of the renewed framing defect is unmeasured. Do not label it harmless noise
or operator error without evidence, and do not erase the rejected events.

3756 dual-capture frames match,2367 RPDOs all zero,36 volatile writes/full baseline
restoration,9534 trace records,1043 SBUS frames,
one expected discontinuity,no feedback error,CAN error/drop counters unchanged.
Operator OFF confirms stationary wheels,no abnormal sound,drive power OFF.
Optional actual-operation field is empty; no detailed hand sequence is inferred.
Raw silence portion is verified,full F5 recovery remains OPEN. A1/A2 unchanged.
See analysis.json,silence-only-analysis.json,incomplete-analysis.json and raw logs.
