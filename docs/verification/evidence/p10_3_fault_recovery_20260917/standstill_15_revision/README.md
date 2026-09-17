# Revised speed-feedback criterion and single-wheel closure

User confirms the stopping negative feedback is internal drive speed-estimation fluctuation,
classifies it as normal and explicitly closes the single-wheel motion acceptance item.
The closure is based on confirmed left/right direction and physical stopping, no abnormal
sound/opposite-wheel motion, and revised offline review of both A1 captures at±1.5rpm.
See single-wheel-revised-audit.json. Source of cause attribution: explicit operator/user
confirmation, not a new sensor-level measurement. Original raw data and original failures
remain preserved; the older-3.6rpm sample is not numerically inside the new±1.5rpm band.

Future control HIL CLI defaults to15 tenths rpm and accepts explicit0..15 for zero,
recovery, restoration and motion; input-only has no feedback-tolerance option. Internal
library defaults stay0. Runtime accepts the explicit15 limit without changing its default.
Start/zero/idle-other-axis/stop/SDO readback share this band. Motion-phase small feedback
inside the same near-zero band is treated as estimation noise, including the startup-to-
motion boundary; actual movement requires positive feedback>1.5rpm on multiple fresh frames.
Below-1.5rpm remains out of the positive-motion envelope. Positive deceleration feedback
above1.5rpm does not yet satisfy standstill. Stop needs both axes within band>=150ms plus
another fresh frame within1s. No raw clamping, target tolerance, limit/time expansion,
automatic rearm or automatic retry was added. Targets remain right<=5rpm/<3s, other0.

Signed±1.5 endpoints and±1.6 rejection are tested in trace, scalar/packed SDO and the real
virtual executable (including nonzero stop noise). Existing strict scenarios explicitly
select0, preserving their meaning. See build/test/static/cross logs. New F3 A3 uses the
explicit15 CLI value and its own immutable artifact/hash/one-shot scope.

Single-wheel motion acceptance: CLOSED under revised operator-approved criterion.
F3 X1 causality remains separate; A2 still missed the moving-X1 window. P10.3 OPEN.

Final verification: Debug43/43, ASan/UBSan43/43, no skips; scoped static, locked cross,
ELF audit and target smoke passed. F3 A3 physically passed with3652f15b; see ../motion_x1_a3.
