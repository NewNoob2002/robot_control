# P6.6 communication watchdog and physical feedback-loss preparation

Checkpoint update: the operator accepted the manual TPDO feedback test, including
small left-speed excursions. Vibration is a proposed cause, not a proven fact.
See [the checkpoint](PHASE6_CHECKPOINT.md); remaining hardware tests wait for its
commit, push and remote CI. Historical evidence paths resolve through
[the evidence index](evidence/README.md).


Next-step procedure: [watchdog timing and recovery review](P6_6_WATCHDOG_TIMING_RECOVERY_REVIEW.md).
It now prefers validated passive speed feedback for timing and separately reviews
candidate zero-first recovery. It authorizes no mapping change, higher speed or run.

Date: 2026-09-10. Status: software validation PASS; watchdog attempt 2 executor PASS,
physical acceptance OPEN; heartbeat/TPDO loss not yet run.
These are three separately selected trials. Prior Quick Stop authorization and
operator observations do not establish their physical results.

## Fixture and scope

Identified ZLAC8015D V4, node 1, RK3588 development target via the existing
robot-dev SSH alias, Classical CAN can0 at 500 kbit/s. Independent JCAN
serial 207F346D5650 remains receive-only in silent mode. Each trial commands
only independent target 0x60FF:02 = +5 rpm; subindex 1 remains zero.
Raised/unloaded wheels, released brake, a present operator with an independent
power cut, and no other CAN transmitter are prerequisites to renew before HIL.
No persistent save, fault reset, automatic re-enable, input/output actuation,
CAN configuration change, or transmission from JCAN is part of these trials.

## Contract evidence and unresolved behavior

The supplied vendor v1.01 PDF, PDF page 27 (printed page 26), documents
0x2000:00 as U16, range 0..32000 ms, default 0, recommended 200..1000 ms.
Its description says the motor stops when no host instruction arrives within
the configured interval. The table was visually checked. Exact timer-refresh
frame classes, state changes, and restart behavior remain unqualified.

PDF page 26 (printed page 25) documents 0x1800:05 as U16 with 0.5 ms units.
The asynchronous PDO description also permits mapped-data-change triggers.
Therefore writing its event timer to zero is an experimental suppression
stimulus, not proof that all TPDO transmissions must stop. A continued stream
is a failed/inconclusive loss experiment and cannot close the requirement.

The P6.1 accepted inventory records watchdog 0, heartbeat producer 0,
command application 1, and TPDO0 event timer raw 100. Every affected baseline
is read again before motion. Any mismatch inhibits the trial before enabling.
The packed velocity mapping contradiction remains open: independent velocity
SDOs plus operator observation are required; packed zero alone proves nothing.

## Exact executor operations

The separately built Debug-only, default-OFF executable accepts:

| CLI flag | Sole stimulus after the 200 ms +5 rpm lead | Observation |
| --- | --- | --- |
| --watchdog-once | No further host transmission for another 1500 ms | Both independent velocities and packed velocity must read zero before any cleanup zero/controlword/NMT command |
| --heartbeat-loss-once | Verified volatile 0x1017:00 = 0 | Heartbeat expires at 500 ms while TPDO0 raw arrivals remain within 100 ms |
| --tpdo-loss-once | Verified volatile 0x1800:05 = 0 | TPDO0 arrivals expire at 100 ms while heartbeat remains current |

No axis, rpm, duration, or combined-stimulus arguments are accepted for these
flags. Loss observation is capped at 750 ms after suppression/readback.
The motion-evidence correction adds independent velocity reads 0x606C:01/:02
after the 200 ms lead, sharing one 100 ms deadline. It requires zero on the
other axis and nonzero measured velocity on the commanded axis before entering
the quiet window or applying suppression. Missing motion aborts with cleanup;
the target is never renewed or increased to make the test pass.
Each trial first verifies zero targets, zero independent/packed velocities,
mode/display 3, and no fault; temporarily sets heartbeat raw 500 and command
application 0; performs the existing zero-target 6/7/F sequence; then writes
and verifies watchdog 1000 immediately before the single nonzero target.
The 200 ms lead includes the target write and readback. The watchdog quiet
window starts after this lead, so the wire trace must measure the actual gap
from the last host frame rather than assume exactly 1500 ms from target TX.

All TX is standard Classical CAN, no RTR/FD: NMT ID 0, DLC 2, node byte 1;
SDO ID 0x601, DLC 8. The two new exact U16 downloads are:

| Object | Set/suppress payload | Restore payload |
| --- | --- | --- |
| 0x2000:00 | 2B 00 20 00 E8 03 00 00 | 2B 00 20 00 00 00 00 00 |
| 0x1800:05 (TPDO trial only) | 2B 00 18 05 00 00 00 00 | 2B 00 18 05 64 00 00 00 |

Existing exact operations remain NMT Operational/Pre-operational, mode 3,
controlwords 6/7/F and cleanup 6, command application 0/1, heartbeat 0/500,
both zero targets, the sole subindex-2 target +5, and fixed readbacks. No
other PDO object, node, watchdog value, persistent object, or target is allowed.
Each authorization is consumed by one matching frame; no SDO request retry.

## Attribution and cleanup

The watchdog probe itself may refresh the timer and potentially resume a
retained target; this behavior is unknown. Its three reads share a 500 ms
deadline. Any nonzero/error fails immediately into cleanup. Operator observation
must confirm the wheel stopped during the host-silent interval and record any
restart at the probe. Without this observation, an exit code of zero is only
software success and cannot close the physical watchdog requirement.

Heartbeat expiry intentionally invalidates TPDO eligibility in the normal
observation model. Qualification uses raw same-generation TPDO timestamps to
attribute continuing wire traffic; it does not restore motion eligibility.
Unexpected stream loss, EMCY, malformed frames, CAN errors, transport generation
changes, or SIGTERM fail the selected trial. For feedback-loss trials, an
observed state departure from Operation Enabled also fails attribution.

Both zero targets are attempted even if the first fails. Before restoring a
suppressed feedback producer, zero targets and zero velocity are verified.
Then the existing bounded Shutdown/Pre-operational cleanup and application/
heartbeat restoration run. The event timer is restored to raw 100 for the
TPDO trial. Watchdog 0 is restored last, only if cleanup and feedback restoration
both succeeded. Unverified cleanup retains the armed or unverified watchdog,
reports the condition, and requires operator power cut; no re-enable occurs.
A lost acknowledgement to watchdog restoration leaves its actual value
unverified and the trial failed. Restoration success never erases a stimulus
failure. Every completion permanently consumes the session's motion authority.

## Physical acceptance and evidence

For each separately authorized run, capture raw RK3588 traffic with
candump -ta -e and a ready, bounded independent JCAN silent receiver before
starting the executor. Include empty TPDO2..4 frames in correlation. Require
exact ordered frame agreement, zero CAN errors/drops, no unauthorized frames,
no residual processes, unchanged adapter configuration, and exact restored
values. Preserve failures and never automatically retry a physical stimulus.

Measure last host TX to physical stopping/watchdog evidence, last heartbeat
or TPDO to the first cleanup zero, zero-feedback settling, NMT Pre-operational,
and restoration. Feedback-loss cleanup must begin before the backup watchdog
deadline; otherwise the stopping mechanism is not attributable to middleware.
Collect the operator's axis/direction, stop/restart, brake and sound observations.
The watchdog experiment qualifies only complete host silence; refresh behavior
under selective traffic or a crashed process remains a separate limitation.

Software evidence is recorded under
evidence/p6_6_20260910_communication_loss_preparation/. HIL remains pending
until a current exact authorization, capture preflight, execution, analysis,
and operator confirmation are recorded. Run the watchdog trial first and review
its stop/restart behavior before relying on it as backup for the loss trials.

## Completed software validation and current hardware gate

- Qualification host Debug: 51/51 passed, including managed-vcan.
- LLVM 22.1.8 ASan/UBSan: 51/51 passed, including managed-vcan.
- Default Debug and Release: 28/28 each passed; P5.6 regression: 36/36 passed.
- The managed-vcan executable includes 24 new communication scenarios:
  successful stimuli, ineffective watchdog, absent/wrong-stream loss,
  baseline mismatch, failed zeroing/restoration, SIGTERM, and EMCY.
  The separate both-axis cleanup regression and one-frame gate checks pass.
- Fresh pinned RK3588 Debug qualification build and ELF/sysroot audit pass.
  Artifact SHA256:
  a03895cb4bb60a98783e6c705a89ddd91ce7152c5dd337074360a41d176c6c2b.
- Clang analyzer/bugprone/performance review completed with no errors and
  18 advisories: 12 const-return move opportunities, two adjacent-parameter
  warnings, two guarded Result accessor warnings, and two intentional flushed
  startup messages. Exact object/value gates bound the new setting helper;
  no new abstraction is needed for its fixed internal calls.
- Watchdog runner frame-guard self-check passes. Its authorization file is
  intentionally absent. Neither runner has been executed or deployed.
- JCAN self-test/scan/config-get pass for serial 207F346D5650; adapter settings
  match the prior fixture. These operations generated no CAN traffic.
- Target SSH preflight failed with a connection timeout outside the sandbox.
  Target identity, current CAN state, capture readiness, and operator readiness
  therefore remain unverified for this trial. No new physical test has run.

Next authorization is one watchdog trial on the specified node/axis, including
the exact setup, readbacks, cleanup and volatile restoration above. The prepared
watchdog capture guard caps downloads at 20, uploads at 300, NMT frames at 2,
nonzero targets at 1, total captured frames at 10000, and capture at 55 seconds.
Heartbeat/TPDO trials wait for review of the physical watchdog result.

## Physical follow-up on 2026-09-10

Connectivity was restored and the operator authorized the exact watchdog trial.
Attempt 1 aborted before watchdog arming or any nonzero target: both status
halves remained 0x1407 despite acknowledged Shutdown requests. Both targets
and temporary settings were restored, but state cleanup was not verified.
The operator confirmed stationary wheels with no abnormal brake action/sound.
See evidence/p6_6_20260910_watchdog_trial_1/RESULT.md.

After the requested driver power cycle and the operator's readiness response,
attempt 2 passed setup, a 1700.008 ms host-silent interval, zero pre-cleanup
velocity readbacks, and final cleanup/restoration. Both captures agree on
182 frames. A brief independent right-velocity rebound followed the first
probe; final all-zero feedback was verified 205.307 ms after that probe.
An RX accounting discrepancy remains: kernel counters advanced by 122 received
frames while both captures contain 120. The operator subsequently confirmed
that the right wheel stopped during silence and then restarted; the left wheel
stayed stationary, with normal brake behavior and sound. No renewed nonzero
target or Enable Operation request preceded this restart. The observed stop
is not latched, and the exact restart-triggering frame remains unisolated.
RX accounting and exact watchdog timing still prevent full physical closure
despite the executable's successful exit. Heartbeat/TPDO motion trials remain
pending review of this recovery behavior. See
evidence/p6_6_20260910_watchdog_trial_2/RESULT.md and its reproducible analyzer.
The preceding software/preflight section is the preserved preparation history;
these two attempt records supersede its deployment/connectivity status.

The operator then explicitly requested a re-test. Attempt 3 used the same
artifact/stimulus with extra read-only counter snapshots and passed the executor
and restoration checks. Both captures agree on 166 frames; the host-silent gap
was 1700.005 ms. No rebound appeared in sampled velocity feedback. The operator
reported both wheels stayed stationary with normal brake behavior and sound;
this repeat therefore does not demonstrate dynamic watchdog stopping. The two-frame RX discrepancy
reproduced during executor operation, with stable counters across capture
startup/shutdown boundaries. See evidence/p6_6_20260910_watchdog_trial_3/RESULT.md.
Overall qualification and the physical heartbeat/TPDO tests remain open.
The resulting software acceptance fix and its new artifact are recorded in
evidence/p6_6_20260910_motion_evidence_fix/. Historical trial artifacts remain
unchanged and do not contain this additional motion-evidence check.

## Corrected watchdog trial 4 — 2026-09-10

The corrected artifact verified initial independent left velocity 0 and right
velocity raw 52 in 2.317 ms before a 1503.002 ms TX-free interval. Post-silence
velocity reads and final cleanup were zero, with no sampled rebound. The operator
corrected the initial stationary report: the right wheel rotated counter-clockwise
viewed from its right side, then stopped; the left stayed stationary, with normal
brake behavior and sound. Exact stop phase and absence of restart were not
explicitly confirmed. Both captures match all 170 frames; kernel packet and byte
deltas reconcile exactly (58 TX, 112 RX). Cleanup, volatile restoration, adapter
shutdown and process postflight pass. See
evidence/p6_6_20260910_watchdog_trial_4/RESULT.md. Exact watchdog stop timing and
traffic-resumption safety remain open; trial 2's observed restart remains
applicable. Physical heartbeat/TPDO-loss qualification and Phase 6 remain open.

## Manual speed-first TPDO verification — 2026-09-10

The operator-authorized manual-only trial configured the supplied packed-speed-
first/status-second TPDO1 order without target or controlword writes. The right
wheel was turned by hand in both directions; the left remained stationary with
normal brake/sound. Both captures match2042 frames, including1205 TPDO1 samples
at median50.007ms. Right raw feedback spans-1036..920 and returns to zero;
independent SDO speed also changes. Left raw excursions-5..4 in19 samples remain
unexplained despite observed stationarity. Kernel RX has an unresolved excess of
3 packets/24 bytes; no CAN errors/drops occurred. Original mapping and heartbeat
were restored with exact readbacks. This supports manual feedback availability,
not enabled-drive watchdog timing or perfect axis isolation. See
[manual trial](evidence/p6_6_20260910_manual_tpdo_trial_1/RESULT.md).
