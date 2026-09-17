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
