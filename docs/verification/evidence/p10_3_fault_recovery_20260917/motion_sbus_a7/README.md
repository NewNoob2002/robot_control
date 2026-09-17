# F4 A7 — PASS (2026-09-17)

User authorized one rerun after confirming delayed transmitter shutdown in A6.
Unchanged cd8bd1c5/CLI20 artifact,60s total,8s hard motion bound/right<=5rpm/left0.
Target identity, hashes, help and pure control-cycle smoke passed; runner checks
passed. Current local START confirmed fixture readiness; both markers consumed.

Actual frame_lost occurred during3700.051199ms right motion, before7950ms cutoff.
Reader receive→successful zero RPDO syscall49.584us; flags0/4/12 captured. No X1
or nonzero restart; stable±2rpm feedback independently verified280.679762ms after
zero.5437 trace records,feedback_bad0/hard_bad0/review_count0.1807 dual-capture
frames match,1006 RPDOs,36 volatile writes and exact baseline restoration.
CAN error/drop counters unchanged. Application exit1/source_fault3 is expected
protective withdrawal, not an unrelated failed trial. Four missed control periods
and max lateness39.757ms retained as timing observations, not real-time acceptance.

Operator typed OFF, confirming right direction/stop normal,left stationary,no
abnormal sound,drive OFF,transmitter OFF,receiver powered throughout. Optional
actual_operation free text is empty and is not fabricated; button press timing
is unmeasured. Recorded physical confirmations and independent trace support F4
acceptance. A6 remains incomplete and its−3.5/−3.8rpm samples remain pending review.
A5 revised acceptance is unchanged. F5/F6 remain pending; P10.3 remains OPEN.
See acceptance.json, sbus-analysis.json and raw recovery-target/application.log.
