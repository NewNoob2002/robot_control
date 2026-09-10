# Manual TPDO speed capture — 2026-09-10

Outcome: **PASS — manual TPDO speed feedback accepted by the operator on
2026-09-10.** Mapping/capture and restoration checks also pass. The small
left-speed excursions are accepted for this test; chassis vibration is the
operator's proposed explanation, not an established cause. The RX accounting
discrepancy remains a separate evidence issue. Enabled-drive watchdog timing
was not tested.

The operator confirmed turning only the right wheel, clockwise and
counter-clockwise, with the left wheel stationary and normal brake behavior and
sound. No timestamped direction markers were supplied, so this record does not
assign positive/negative feedback to a particular physical direction.

The Debug-only application required zero targets, initially zero speed, no fault,
and dual ReadyToSwitchOn or SwitchOnDisabled. It wrote no motor target or
controlword. It configured TPDO1 as packed speed 0x606C:03/32 followed by status
0x6041:00/32, type255 and event timer raw100, then entered NMT Operational for
60 seconds. Temporary heartbeat500 supported supervision. Periodic SDO uploads
were diagnostic comparisons, not a watchdog-silence test.

- 2,042 exactly matching RK3588/JCAN frames; 1,205 TPDO1 speed samples.
- TPDO cadence: minimum49.778 ms, median50.007 ms, maximum50.247 ms.
- Right packed speed raw range -1036..920; nonzero in 1,043 samples. Both signs
  appeared during the operator's two-direction hand rotation.
- Left packed speed raw range -5..4; 19 nonzero samples despite operator-observed
  stationarity. The operator explicitly accepts these excursions for this manual
  feedback test. Preserve the raw values without claiming perfect axis isolation.
- Independent right SDO reads also changed. Per-sample nearest-TPDO comparisons
  are in analysis.json; asynchronous differences are not an accuracy/scale test.
- Final TPDO samples and the final three speed SDO readbacks were zero; dual
  status remained raw0x14211421 (ReadyToSwitchOn).
- Exactly18 configuration downloads,276 uploads,3 NMT frames, and zero target,
  controlword, watchdog, persistent, or JCAN transmissions.
- Kernel TX reconciles:297 packets/2358 bytes. Kernel RX advanced1748 packets/
  12259 bytes; both captures contain1745 RX packets/12235 bytes. The excess is
  3 packets/24 bytes during executor operation; capture-boundary counters were
  stable. No CAN errors/drops were reported. Initial strict counter assertion
  failure is preserved in analysis_initial_failure.log.
- All18 downloads had matching ACKs and exact readbacks, including restoration
  of status-first/speed-second mapping, enabled COB-ID0x181, type255, timer100,
  count2, and heartbeat0. Final NMT was Pre-operational; target readbacks stayed0.
- JCAN shutdown succeeded, adapter configuration was unchanged, no test process
  remained, and target counters stayed stable after cleanup.

The new mapping was restored; it is not left active on the drive. This trial
supports the supplied mapping for manual, non-enabled feedback. It does not
prove why earlier packed feedback was zero, qualify motor-driven feedback, or
resolve watchdog trigger timing/restart behavior. Those require separate review.

Artifact SHA256:
8ca2c0250736e792ecb0b7067973ce2547f7e2d6fb9c38d0147a5788ade34c8a

Reproduce: python3 docs/verification/evidence/p6_6_20260910_manual_tpdo_trial_1/analyze_trial.py
Raw speed series: speed_samples.csv. Operator report: operator_observation.json.
