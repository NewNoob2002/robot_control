# P6.6 watchdog timing and recovery procedure — review draft

Checkpoint update: the operator accepted the manual TPDO feedback test, including
small left-speed excursions. Vibration is a proposed cause, not a proven fact.
See [the checkpoint](PHASE6_CHECKPOINT.md); remaining hardware tests wait for its
commit, push and remote CI. Historical evidence paths resolve through
[the evidence index](evidence/README.md).


Date: 2026-09-10. Status: revised to prefer passive speed feedback; no new
hardware trial, speed increase, PDO mapping change or recovery implementation.
This revision supersedes the earlier camera-first/power-removal timing proposal.
External motion recording is a fallback, not a mandatory prerequisite.

## Evidence and measurement decision

Prefer drive-produced, asynchronous speed feedback captured without host TX
during the watchdog interval. Validate the feedback before using it as an oracle.

The independent 0x606C:01/:02 SDO values have tracked motion. Trial 4 returned
left raw 0 and right raw 52 before silence. However, the existing TPDO packed
velocity has remained zero during known motion. Its mapping/value contradiction
must be resolved before timing from it. See the repository drive notes and
[trial 4](evidence/p6_6_20260910_watchdog_trial_4/RESULT.md).

Do not poll SDO speed during a complete-host-silence test: those requests add
host traffic whose watchdog refresh behavior is unqualified. Trial 2 confirmed
a stop then restart after traffic resumed without a new nonzero target or
Enable Operation. A selective-traffic test would be a different requirement.
See [trial 2](evidence/p6_6_20260910_watchdog_trial_2/RESULT.md).

Do not assume watchdog expiry produces CiA402 Shutdown. Trial 2's status
change from 0x4427 to 0x1427 retained Operation Enabled. Separate loss of motion
from a drive state transition and from a latched inhibit.

## Step 1 — qualify a passive speed stream

The operator supplied a working STM32 sequence: disable TPDO1 via COB-ID bit
31, clear mapping count, map 0x606C:03/32 first and 0x6041:00/32 second,
set count 2, type 0xFF and event timer, then re-enable the COB-ID. This is
the first candidate configuration to review, rather than assuming that new
independent-velocity mappings are necessary. Automatic transmission is the
intended behavior; persistent storage is neither required nor authorized.

The accepted 2026-09-04 inventory already has COB-ID 0x181, type 0xFF,
event timer raw 100 and both objects mapped, with the opposite order:

| Field | Recorded baseline | STM32 candidate |
| --- | --- | --- |
| 0x1A00:01 | 0x60410020 (status) | 0x606C0320 (packed speed) |
| 0x1A00:02 | 0x606C0320 (packed speed) | 0x60410020 (status) |
| Payload bytes 0..3 | Status | Packed speed |
| Payload bytes 4..7 | Packed speed | Status |

Source: [accepted inventory](evidence/p6_1_20260904_continuation_1/fixture_inventory_complete.yaml).
Re-read the live baseline before any write. Remapping may affect the observed
behavior, but order alone does not establish why packed speed remained zero.
Match Linux decoding to the selected layout and test it with nonzero values
that distinguish status from speed. Verify every mapping/communication value
by readback before motion and restore the original layout afterward.

Do not use the STM32 name kTpdoEventTimerMs as evidence of this drive's units:
the reviewed vendor reference describes 0x1800:05 in 0.5 ms units. Preserve
raw 100 for the first configuration comparison and measure actual cadence;
any later timer change needs its exact raw value and reviewed bounds.
Configure in the reviewed zero-motion/Pre-operational setup, then verify
automatic TPDO reception in Operational before the bounded motion comparison.
An SDO failure midway through the short-circuit sequence requires explicit
rollback handling; do not leave a disabled or partially remapped PDO as success.

Review the accepted inventory, raw TPDO payloads, mapping objects and vendor
PDO capabilities locally. Establish whether independent actual velocity can be
emitted asynchronously without polling or host SYNC. Do not assume remapping
support. If a volatile mapping change is required, prepare its exact object,
payload, order, bounds and restoration manifest before separate authorization.
No persistent save or unrelated configuration change is included.

At the existing +5 rpm unloaded condition, compare candidate passive feedback
against independent SDO values and operator direction/axis observations before
the silence experiment. Require correct axis, sign, scale, nonzero motion and
subsequent zero. Use SDO comparisons only outside the silent interval. Record
actual sample intervals, gaps, device update/filter behavior where known and
raw values. Packed zero in known motion fails feedback qualification.

If no trustworthy passive speed stream is available, stop this approach and
review external sensing or an explicitly different traffic-refresh experiment.
Do not silently substitute SDO polling or increase speed to hide a mapping defect.

## Step 2 — measure the speed response during complete host silence

Keep the identified ZLAC8015D V4/node 1, robot-dev/can0 Classical CAN 500 kbit/s
and JCAN 207F346D5650 silent RX. Retain raised/unloaded wheels and independent
operator power cut. Begin with the existing +5 rpm right-axis limit, left target
zero, raw watchdog 1000, zero setup, 200 ms lead and <=100 ms independent
motion check. No target renewal or automatic re-enable.

Start both bounded captures before setup. After verified initial motion,
transmit nothing for the existing 1500 ms observation window. Capture drive
speed and status passively. Any host SDO, SYNC, NMT or other host frame during
that window invalidates complete-silence attribution.

Use recorded CAN timestamps, not the time a userspace loop happens to read a
frame. Verify timestamp source and the meaning of local TX/echo timestamps on
this interface; do not call a software echo an exact physical delivery time.
Linux kernel receive timestamps avoid incorporating the later userspace read
schedule, but driver/interrupt latency, timestamp source and clock changes still
need consideration. See Linux kernel networking/timestamping documentation.
Trial 4's JCAN timestamp fields were zero: use that capture for frame agreement,
not an independent timing reference unless its timestamp capability is verified.

Report these separately:

| Measurement | Meaning |
| --- | --- |
| Last host TX to first sustained speed decline | Observed response onset; estimate of watchdog effect, not its internal trigger instant |
| Last host TX to sustained near-zero speed | Completed stopping response, including deceleration |
| Last steady sample to first declining sample | Sampling bracket for response onset |
| First resumed host TX to any renewed speed | Recovery/restart behavior |

Before the run, set numerical decline/zero thresholds and persistence counts
from the qualified stream's scale, baseline variability and sample cadence.
Publish the timing uncertainty from sampling, filtering and capture timestamps;
do not promise arbitrary millisecond precision. A speed sample cannot directly
expose an internal timer event. Do not equate raw 1000 with an experimentally
proven 1000 ms completed-stop deadline.

A proposed speed increase is a separate envelope change. First assess whether
+5 rpm gives a useful passive speed curve. A larger signal might improve decline
detection, but cannot fix sample cadence, timestamp accuracy, feedback mapping
or unknown filtering, and changes the deceleration experiment. Choose an exact
speed/duration only after reviewing the stream and mechanical envelope; obtain
separate authorization and update the fixed executor/gates before running it.

## Step 3 — review recovery independently

The current executor probes with SDO uploads before zeroing. That behavior can
resume a retained target and is not a qualified safe recovery policy. Prepare
and test a candidate recovery order before the next watchdog timing trial; do
not execute the old runner as if timing acceptance resolves its restart risk.

Candidate: first resumed host frame is right-axis zero, standard 0x601/DLC 8,
payload 23 FF 60 02 00 00 00 00; then left-axis zero
23 FF 60 01 00 00 00 00, then Shutdown 2B 40 60 00 06 00 00 00,
followed by fixed diagnostic reads and existing bounded cleanup/restoration.
No upload, left-axis write or NMT precedes clearing the retained right target.
This is unproven: even the first zero request may refresh the watchdog before
zero takes effect. Watch for renewed velocity throughout recovery.

Publish per-request/overall deadlines, exact ACK-failure paths and frame/count
bounds in the implementation review. Preserve both-zero attempts, no retry or
re-enable, and operator power cut for uncertain stopping. Restore watchdog 0
last only after verified cleanup. If power is removed, mark volatile restoration
unverified and re-inventory before any later powered trial.

Any observed rebound fails candidate recovery even if cleanup succeeds. An
absence of rebound only supports the declared stream resolution and this tested
condition; it does not establish arbitrary-traffic or production recovery safety.

## Release and acceptance gates

Before another physical run, complete passive-stream qualification design,
exact setup/restoration manifest, measurement thresholds and timing uncertainty,
recovery implementation, artifact hash, and focused host/managed-vcan tests.
Test missing/stationary/wrong-axis feedback, TX-free silence, recovery frame order,
lost ACKs, both-zero attempts, cancellation and no retry/re-enable. Run applicable
sanitizer, isolation, cross-build and static checks for changed code.

Retain existing caps unless explicitly reviewed: one nonzero target, 20 downloads,
300 uploads, 2 NMT frames, 10000 captured frames, 55 s capture. These caps are not
permission for new mapping writes or higher speed. Fresh authorization must name
the exact physical profile; this draft performs no hardware operation.

Require exact ordered RK3588/JCAN frame agreement, packet/byte reconciliation,
clean CAN errors/drops, raw speed/status evidence, operator observation, verified
cleanup/restoration, adapter shutdown and no residual processes. Classify missing
feedback as inconclusive, measured late/no stop as failure against the agreed
bound, and any recovery rebound as a separate failure. Preserve prior results.
Physical heartbeat/TPDO-loss qualification remains separately gated.

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
