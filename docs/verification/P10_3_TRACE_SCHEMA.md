# P10.3 bounded qualification trace v1

Debug-only tool evidence. Trace storage is preallocated/zero-initialized before
opening devices:65536 fixed144-byte records (9MiB on the verified toolchains).
No allocation or output occurs on append. Overflow latches failure and never
overwrites evidence. Export occurs after the bounded stop/cleanup path. Header,
all contiguous ordinals and matching footer are mandatory. Dump failure fails
the invocation. After stop/cleanup, serialization uses temporary heap storage
(content capped at32MiB+4KiB), then writes with a5s monotonic deadline and bounded
poll on EAGAIN. This allocation/I/O never occurs in the active control loop. This is not a general telemetry or production logging interface.

All row timestamps are steady-clock userspace nanoseconds. SBUS rows use the
Reader batch receive time, not individual-byte/transmitter time. CAN RX means a
successful MSG_PEEK by the existing sole owner; subsequent protocol acceptance is
separate. TX rows record the requested frame and syscall result, not proof of bus
delivery. Target candump and silent JCAN remain independent bus evidence; their
clocks are not subtracted from the application clock.

Rows have kind, ns and16 signed integer fields; unused trailing fields are zero.

| Kind | Ordered fields |
| --- | --- |
|0 batch|batch id, Reader session, discontinuity enum, kernel raw byte count, parser event count|
|1 frame /2 rejected|batch id, session, event ordinal within batch, CH1, CH3, CH6, CH7, flags, parser event kind; channel data are meaningful only for frame|
|3 cycle|cycle count, Source sequence, authorization generation, Source session, Source captured_ns, normalized steering/throttle, candidate left/right, selected left/right, approved request left/right, drive action enum, Source enabled, Source health|
|4 CAN peek /5 TX attempt|CAN id including flags, DLC, eight raw bytes, syscall return, errno (zero on successful syscall)|
|6 stop|cause code, LifecycleExit enum before explicit stop, last Source sequence, last Source captured_ns|

Stop causes:0 operator window ended;1 first zero target closed motion window;
2 automatic2950ms cutoff;3 gate rejection/closure;4 runtime finish/signal (inspect
LifecycleExit);5 envelope/transport error;6 diagnostic/feedback failure;
7 completed zero-only recovery scenario. Cause1
alone does not prove physically neutral sticks: inspect the normalized axes and
candidate/request records. Failed syscall requests never count as bus delivery.

R0 uses exactly the motion trial calibration/profile in receive-only mode:
steering200/1000/1800, throttle200/993/1800, neither reversed, all gears5rpm,
input deadband50/1000 and output deadband3rpm. It creates no CAN Lifecycle;
the final send wrapper additionally denies every send while R0 is bound. Cycle
rows in R0 are input snapshots: selected/approved request fields are zero because
there is no control/drive policy owner. CH6 authorization terminates R0 as failure.

The optional ControlLoop::step captured_batch output copies the actual successful
read, clears caller storage on early exit, and never participates in policy.
Main records every parser event after step, so record ordinal is append order,
not a claim that raw receive time follows the preceding CAN TX timestamp.
Use Source captured_ns/session to correlate the batch and candidate.

The CAN peek wrapper latches the original feedback envelope across individual
frames, including during stop. Main stops at its first observation of that latch
and reports failure; independent capture remains the full-wire oracle. The
recorder is observational, not a new asynchronous safety authority. Stop keeps
reading UART without restarting the stopped Source or permitting new commands.
One additional fresh zero frame is retained after the150ms stable-zero condition
for independent evidence margin; the physical-stop deadline remains1s. No new
negative-feedback tolerance is introduced.

The analyzer scripts/test/analyze_control_hil_trace.py verifies completeness and
raw/candidate linkage. A complete failed-bootstrap trace may contain only CAN
records; an actual R0 or control trial must additionally have raw frames/cycles.
Trace completeness does not imply drive acceptance. Report the original failure,
source flags, final restoration and operator disposition separately.

## Prospective zero-feedback tolerance metadata

The version1 header additionally records zero_feedback_tenths_rpm (0..10).
Absent means0 for historical traces. Nonzero is permitted only for explicit
zero-target qualification. The recovery oracle takes an independent expected
threshold; header mismatch rejects. Raw signed feedback fields are unchanged.
Prior motion/zero failures are never reclassified by this metadata.

## Explicit motion window (F4 A4)

The user authorized a larger F4 window for a long-press transmitter power button.
Debug-only motion CLI accepts --motion-window-ms3000..8000, default3000.
event=control_start records motion_window_ms; event=motion_ready records
maximum_nonzero_ms. Both the owner cutoff and independent send gate use this
same duration. Stop cause2 now means configured duration minus50ms (default2950,
A4 explicitly7950); hard nonzero rejection is at the configured duration.
The bounded trace format/raw timestamps remain unchanged. Independent motion
feedback analysis defaults to3000 and requires an explicit expected value for
extended trials; A4 checks metadata against its8000ms authorization.
Near-zero feedback uses the subsequently approved default15 tenths rpm in control
modes; historical0/10 criteria and original failures remain unchanged.

## Startup input readiness (F4 A5)

event=input_status reports state=waiting_link|neutral_required|release_ch6|ready|arming,
source_fault and raw flags while the right-throttle qualification awaits initial
authorization. These are operator reminders only; no Source sample or authority is
injected. Healthy disabled Source, neutral axes and released CH6 precede ready;
Source still requires a fresh CH6 edge. The original duration bounds all waiting.
Invalid/disabled raw candidates remain visible and cannot authorize nonzero output.
After enable, authority loss still ends the session; reminders do not rearm it.
The motion-fault oracle must use a fault timestamp after first nonzero, preserving
earlier startup flags separately rather than counting them as the motion stimulus.


## Explicit ±2rpm revision (2026-09-17)

User approved raising the startup/stop feedback band to±2rpm. Control HIL CLI
--zero-feedback-tenths-rpm now accepts0..20 and defaults20; library defaults
remain0. Historical analysis defaults and recorded15 criteria are retained;
revised analysis must explicitly pass20. Raw feedback and original failures
are never rewritten. Positive motion limits, zero-command requirements,
>=150ms stable holds and fault timing/rearm rules remain unchanged.
F4 A5 is accepted by retrospective analysis under20, while its original15
failure remains preserved. No new physical test occurred for this revision.
See standstill_20_revision/README.md in the fault-recovery evidence directory.


## Deferred selected-wheel stop feedback review

Following a successful zero RPDO after motion, selected-wheel negative speed
below the near-zero tolerance but >=−75 tenths rpm is a review item provided
the opposite wheel remains within tolerance. CAN RX fields[12]=1 records this
classification. Raw feedback_bad stays latched; new feedback_hard_bad controls
fatal trace checks and feedback_reviews counts deferred samples. After trace_end,
feedback_review_summary and feedback_review rows list ordinal, timestamp, wheel,
signed tenths_rpm, tolerance and verdict=pending. Existing raw records/version1
remain parseable. Review output is bounded by trace capacity/export deadline.
Active motion constraints and preflight/cleanup checks are unchanged. The1s stop
observation still requires >=150ms stable near-zero feedback; persistent failure,
absolute stop feedback beyond7.5rpm, opposite-wheel motion or capture loss is fatal.
Pending review is not automatic acceptance; historical failures remain unchanged.


## F5 partial-frame boundary (A2)

Dedicated --zero-uart-recovery may recognize one empty partial_timeout batch only
in initial fault_ready. event=uart_boundary kind=partial_timeout at_ns=... session=...
references that exact Reader timestamp and incremented session; raw batch rows
are unchanged. Source still revokes immediately and reports discontinuity. The
observer requires no bytes during the >=1s fault hold and fresh neutral rearm after
reconnection. Service-gap/backlog/transport faults, additional discontinuities and
RF-loss flags are not accepted as F5 stimulus. F2 uses its unchanged strict observer.


## F5 extended coordination and reconnect acquisition

F5 alone supports a120000ms total window, minimum5s raw silence hold and45s
reconnection/challenge deadline. event=uart_resync at_ns=... authority=revoked
records the first rejected acquisition candidate; raw rejected events remain in
trace. event=uart_reconnected at_ns=... authority=revoked marks fresh healthy
Disabled input and triggers INPUT_RESTORED. Only before this first healthy mark
may phase2 ignore parser-candidate rejection as an acceptance error; it never
suppresses Source revocation. Later rejection and hard stream errors still fail.
The header/schema, bounded capacity/export and raw Source records stay unchanged.


## F5 continuous stable reconnect (A4)

Phase2 requires >=1s fresh healthy Disabled/neutral input with CH6 released,
measured using received sample timestamps. uart_reconnected adds stable_since_ns;
rejected/stale/recovering input resets the timer. Phase2 parser rejection remains
recorded and revoked, including after an earlier stable prompt; nonneutral challenge
progress resets. uart_resync invalidates the earlier operator cue and subsequent
uart_reconnected emits a new cue. Independent audit rejects a candidate rejection,
nonneutral/enabled source or missing fresh frames inside each claimed stable span.
Only phase2 gets this bounded retry; fixed45s deadline and hard faults are unchanged.


## G3 signed qualification modes — 2026-09-18

The bounded v1 text header adds motion_mode (0 input-only,1 zero,2 left-positive,
3 right-positive,4 left-negative,5 right-negative,6 dual-positive,7 dual-negative,
8 turn-left,9 turn-right,10 differential-left,11 differential-right). Existing
record field positions and binary zero-soak framing are unchanged. Raw signs are
retained. Deployed artifact5f44146c has no G3 deferred stop-feedback exception;
legacy positive single-wheel review behavior is unchanged. The2026-09-18
user-approved acceptance revision classifies bounded startup/stop feedback
transients as normal when the other trial gates pass; see the G3 trial sheet.
This does not change trace semantics: right-negative A1 retains exit1,
feedback_bad=1 and feedback_hard_bad=1 under its original guard. Its current
acceptance is PASS_USER_APPROVED_START_STOP_TRANSIENT in a separate revision
record, never an edited header or claim of an updated runtime. motion_stop adds left_moving_feedback and
right_moving_feedback. moving_feedback counts fresh samples where every selected
wheel simultaneously exceeds the near-zero band in its required direction;
two such samples are required for normal success.
Submission-time qualification rejection follows normal ControlLoop shutdown.
Stop cause4 alone does not imply SIGTERM: inspect LifecycleExit and the primary
operation. See the [G3 trial sheet](../plans/G3_RAISED_MOTION.md).

## Physical right-axis binding — 2026-09-18 polarity correction

New HIL text headers add binding_version=1 and right_sign=-1|1; both must be
present together for new records. Historical headers lacking both mean legacy+1.
Raw CAN TX/RX rows and signed raw bytes stay unchanged. Candidate/selected/approved
and RuntimeState wheel speeds are vehicle-coordinate values. The right wire
target and raw right feedback are multiplied by right_sign at their respective
boundaries; left remains+1 with low-half target/feedback binding.

MotionGate normalizes the right raw target before logical profile checking;
Trace normalizes only for envelope checks and preserves raw rows/review values.
The fixed binary stream header uses field2 for right_sign (version and tolerance
positions unchanged); diagnostics_start includes that field.

CLI --right-sign -1 selects the identified RK3588 vehicle-forward mapping.
Physical motion on can0 requires this option explicitly; invalid/duplicate or
missing physical-motion binding fails before opening devices. Legacy/default+1
remains available for zero-only and explicit virtual fixtures; receive-only
--observe-input rejects the option. The offline auditor accepts --right-sign
to require the trial binding and compares logical relationships after normalization,
while dual-capture payload comparison remains raw/exact. This change does not
modify the previously documented startup/stop transient acceptance policy.

## Differential approach qualification — 2026-09-20

Modes10/11 now allow positive equal-wheel approach while preserving the requested
inequality direction (left<=right / right<=left). motion_stop adds
differential_feedback: distinct owner-observed TPDO samples strictly after the
current unequal-target interval began, with both normalized speeds above the
near-zero band and the requested measured speed ordering. Normal differential
success requires at least two such samples; straight-only motion fails with
control_hil_no_differential_feedback. Other modes report zero in this field.

The raw trace header/rows and binding remain unchanged. The offline differential
auditor requires matching command-before-feedback evidence before first zero,
counts distinct timestamps and reports differential_feedback_samples; its count
need not equal the owner count because it audits every captured TPDO. It still
checks exact targets, no restart, hard3s measured from FIRST nonzero (including
straight approach), bounded standstill, exact dual captures and restoration.
Old artifacts/failed trials retain their original interpretation and verdicts.
