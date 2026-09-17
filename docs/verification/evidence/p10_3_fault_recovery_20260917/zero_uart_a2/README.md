# F5 UART A2 — INCOMPLETE (2026-09-17)

User authorized half-frame observer repair and one A2.13ccbd9d/CLI20 dedicated
zero-uart-recovery used once after fresh START. Partial timeout was recognized,
immediate Source revocation retained and1009.991ms silence hold completed.
After RELEASE_FAULT, two rejected parser candidates appeared1789.982ms later;
phase2/source_fault2 caused exit. Total window still had about50s remaining.
This is reconnect acquisition handling, not total-window expiration.

1694 dual frames match,935 RPDOs all zero,36 writes/full baseline restoration,
4490 trace rows,925 SBUS frames,one expected discontinuity,no feedback error or
CAN counter change. Operator OFF confirms stationary wheels/no abnormal sound;
actual-operation free text is empty, so no detailed hand sequence is invented.
Both runners consumed; original evidence stays unchanged. F5 remains OPEN.
User requests longer disconnect/recovery timing and window. Subsequent offline
preparation uses5s minimum hold,45s reconnection/challenge,120s total and bounded
initial parser reacquisition. It does not change this A2 result or permit a retry.
See analysis.json and incomplete-analysis.json.
