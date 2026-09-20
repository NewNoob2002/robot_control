# G3 left-negative A1 — PASS (2026-09-18)

Unchanged artifact5f44146c, fresh local START after zero A2 prerequisite.
Raised/unloaded left-negative only,<=5rpm targets,<3s motion; approved measured
near-zero band+/-2rpm. Live CONTROL_READY then MOTION_READY cues were displayed;
operator coordinated backward throttle plus left steering. No fault stimulus.

Independent analyze-motion.py passes:1629 globally identical dual-capture frames,
36 allowed volatile writes and full baseline restoration;882 control cycles,
633 enabled samples and1297 SBUS frames. Nonzero window2959.966524ms on the
application clock (wire2960.199680ms),58 fresh left-direction feedback samples,
stop-to-stable verification266.153116ms (wire265.572096ms). No restart.
Raw left feedback-5.7..+1.5rpm; right-0.2..+0.4rpm, always within the uncommanded
+/-2rpm band. Raw values preserved. Internal shutdown22us;3 missed periods,
max lateness35.104ms remain recorded, with no hard-real-time claim.

Application exit0; exact zero/final Disable Voltage and original volatile baseline
verified. CAN error/drop/controller counters unchanged. JCAN receive/ACK only,
clean shutdown, no reconnect/data send. Local OFF confirms left reverse and stop
normal, right always stationary, no abnormal sound and drive OFF. All one-shot
markers consumed. No loaded/ground, dual-wheel or fault acceptance implied.

Raw: physical-once/, target-result.tar.gz, jcan/session.jsonl, operator-off.json.
Audit: analysis.json; replay from repository root with
python3 -B docs/verification/evidence/g3_raised_motion_20260918/left_negative_a1/analyze-motion.py.
Trial/safety/authorization and offline runner checks are retained separately.
