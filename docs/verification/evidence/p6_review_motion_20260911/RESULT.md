# Online attachment zero and single-axis trials — 2026-09-11

The existing authorized zero, left and right runners each executed once. The
second approval request round released the zero runner; the preceding two
approval timeouts created no process. No physical trial was retried.

| Trial | Matching frames in both captures | Matched SDO transactions | Target TX delta | Nonzero target to zero request | SDO speed raw range, left / right |
| --- | ---: | ---: | ---: | --- | --- |
| zero | 107 | 45 | 47 | none | 0 / 0 |
| left +5 rpm | 217 | 60 | 62 | 3000.129 ms | -15..22 / 0 |
| right +5 rpm | 213 | 60 | 62 | 3001.012 ms | 0 / -12..41 |

Each application and coordinator exited 0. Captures match frame-for-frame in
order; every SDO request has a matching non-abort response. Target CAN remained
ERROR-ACTIVE with zero errors/drops. Final readbacks show both targets, both
independent speeds, packed speed and fault value zero. Pre-operational heartbeat
was observed and producer heartbeat was restored to zero. Both motion trials
restored 0x200F to 1. Capture and executor processes stopped. JCAN was silent
and its coordinator verified unchanged adapter configuration.

**Physical motion/feedback qualification remains incomplete.** All TPDO1 packed
speed samples were zero during both powered trials, while independent SDO speed
reads varied on the commanded channel. The traces do not establish the reason,
physical wheel direction, or mechanical acceptability. The operator subsequently
confirmed that both the left and right wheels rotated in their trials; direction
and mechanical acceptability were not additionally stated. Command-to-zero intervals above measure CAN requests, not wheel stopping
time. No watchdog, physical loss, moving SIGTERM or interface-loss trial was run.

Exact commands and machine-readable verification are in each trial directory
under `postflight_verification.json`; raw evidence is in `target/rk3588_can.log`
and `jcan_session.jsonl`. The artifact SHA256 is
`ef9837d751f9038ddadfa76b42107a526b740e6d65f84f0aa8e468ccd236fff0`.

Follow-up timing analysis: [TPDO diagnosis](TPDO_DIAGNOSIS.md). The nonzero SDO
samples in these two trials were collected after the zero command, not during
the three-second motion interval. They must not be reported as measured running
speed ranges or a simultaneous three-object comparison.
