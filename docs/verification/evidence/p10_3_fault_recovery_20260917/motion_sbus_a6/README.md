# F4 A6 — INCOMPLETE; stop feedback pending review

cd8bd1c5/CLI20, right<=5rpm,8s hard bound. Automatic cutoff at7950.034106ms;
first moving-phase SBUS loss flag arrived187.905601ms after zero, so not a
loss-triggered stop. User subsequently confirms late transmitter shutdown.
Startup OFF/readiness prompts worked. Stop samples−3.5/−3.8rpm at161.770/211.823ms
were retained as pending review without abort; stable stop confirmed,2544 dual
frames match,1512 RPDOs,36 writes/full baseline restoration, no CAN counter change.
8026 trace records,2223 SBUS frames,4 missed periods/max lateness41.819ms retained.
Operator OFF/right direction+stop normal/left stationary/no abnormal sound,
receiver powered/no X1 confirmed. Both runners consumed. Original records and
strict F4 oracle rejection retained. No physical reversal/estimator diagnosis
is established from these samples alone. See analysis.json and audit-result.py.
