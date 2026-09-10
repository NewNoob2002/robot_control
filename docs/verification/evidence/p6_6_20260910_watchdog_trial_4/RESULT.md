# Corrected watchdog trial 4 — 2026-09-10

Outcome: executor and capture checks PASS; full watchdog qualification remains open.

The operator confirmed right-wheel counter-clockwise rotation viewed from its right side, then a stop; the left wheel stayed stationary, with normal brake behavior and sound. This corrects the initial both-stationary report. Exact stop phase and absence of restart were not explicitly confirmed.

- Verified artifact: ea1fba5cb9fa3c6e5ede192820b4a4ddc6af575a404f46e19d8290497e7b2ea5. One authorized run, no retry.
- Before silence, independent velocity readbacks were left raw 0 and right raw 52; the check completed in 2.317 ms against its 100 ms limit.
- Host TX-free interval: 1503.002 ms, anchored to the final initial velocity request.
- All three post-silence velocity readbacks were zero before cleanup. No nonzero velocity was sampled during cleanup. Final all-zero feedback was verified 36.316 ms after the first post-silence probe.
- One right-axis +5 rpm target; no renewed target or Enable Operation. Cleanup restored targets to zero, watchdog to 0, heartbeat to 0 and application setting to 1; Pre-operational heartbeat and restoration completed by 157.351 ms after the probe.
- RK3588 and JCAN agree exactly on all 170 frames. Kernel deltas reconcile: 58 TX/452 bytes and 112 RX/802 bytes. The earlier two-frame RX discrepancy did not recur; earlier unexplained records remain preserved.
- No CAN errors/drops, unchanged adapter configuration, successful JCAN shutdown, no residual test processes, and stable post-cleanup counters.

Limits: the raw status-bit transition is not a qualified stop timestamp or watchdog-unit measurement. Trial 2's confirmed stop/restart remains evidence that this is not a latched inhibit. This repeat does not establish safe traffic resumption. Physical heartbeat/TPDO-loss trials remain unexecuted and separately gated.

Reproduce: python3 docs/verification/evidence/p6_6_20260910_watchdog_trial_4/analyze_trial.py
