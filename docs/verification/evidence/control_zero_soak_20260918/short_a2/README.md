# Short A2 — PASS, 2026-09-18

Repaired supervisor a233889 with the unchanged bec74e79 control/9a226adb diagnostic
artifacts. Fresh local START and OFF confirmations are retained; both runners
are consumed. Continuous control600.288s,60001 cycles,60002 all-zero RPDOs,
85709 raw SBUS frames and86770 exact target/JCAN frames. The audit independently
checks36 SDO downloads and13 restored objects against their initial readbacks.
No nonzero targets or out-of-band feedback; near-zero feedback uses±2rpm.
Four missed periods/max lateness39975us/max cycle903us are retained, not hidden.
No hard-real-time or moving/loaded qualification is claimed. RSS sampled at3364KiB
remains stable after startup; process and capture exit normally, operator confirms
stationary/no abnormal sound/drive OFF.

A1 remains failed. The separate one-hour trial is still pending its own complete
evidence and acceptance. This short result does not supply future readiness.
Raw target files are in target-result.tar.gz. JCAN session.jsonl is byte-preserved
as session.jsonl.gz; raw-storage.json supplies hashes/extraction mapping.
