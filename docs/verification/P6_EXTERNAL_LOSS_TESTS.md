# P6.7 SIGTERM and external loss tests

Dates: 2026-09-11 through 2026-09-14. Scope: the first four remaining tests
authorized by the operator and their separately authorized follow-up trials.

| Test | Disposition |
| --- | --- |
| Moving SIGTERM | PASS: dual capture, bounded cleanup, operator accepted |
| Controlled interface down/up | PASS: v5 on 2026-09-14, verified zero/disabled recovery and operator acceptance; earlier failures preserved |
| RK3588 CAN branch disconnect/reconnect | FAILED 2026-09-14: controller errors and zero cleanup timeout; operator confirms stop/no restart and subsequent drive power-off |
| Drive-only power loss/restoration | PASS: V2 used normal-mode JCAN ACK with zero JCAN data-frame commands; 352 exact matching frames, zero-first recovery, verified cleanup and operator acceptance. Attempt 1 remains invalid |

## Moving SIGTERM

Artifact SHA256: dc639ff99b7031b2137c6e23eb235ec3e0a3c635d9781dd10923072c02306a38.
The consumed trial is in evidence/p6_external_loss_20260911/moving_sigterm_once.
RK3588 and silent JCAN captures contain the same 135 frames. One right +5 rpm
packed target was sent. SIGTERM was delivered after nonzero TPDO feedback.
The executor exited 1 as expected with qualification_owner_exit and cleanup=verified.
First zero request followed the signal by approximately 0.384 ms; Shutdown by
11.412 ms; the last of three independent/packed zero speed replies by 234.709 ms.
Final TPDO shows zero speed and dual 0x1421 status. There was no subsequent
Operational or enable command. Signed deceleration rebound is present; these
sampled observations do not prove monotonic deceleration or continuous zero.
The operator confirmed right-wheel motion then normal stop, stationary left wheel,
no unintended restart or abnormal sound, and both wheels stopped with the site safe.
Re-run the offline check with:

    rtk proxy python3 docs/verification/evidence/p6_external_loss_20260911/analyze_sigterm.py

Host and sanitizer suites for that artifact passed 60/60; clean qualification,
default Debug/Release cross builds, ELF audits and target isolated-vcan passed.
These results predate the external-loss implementation under development.

## External-loss software work

A Debug-only fixed external stimulus passed software verification. It retains the existing
zero/mode/fault preflight and temporary 1000 ms drive watchdog. After one right
+5 rpm target it allows at most 3 seconds for loss, then inhibits authority and
waits passively for up to 10 seconds. Recovery sends packed zero before any
upload or NMT request; only zero/disabled cleanup and volatile setting restoration
follow. No automatic motion reauthorization is allowed. Tests cover disconnected
feedback, boot after power reset, real namespace interface down/up, absent loss,
missing recovery and unacknowledged first zero. At this software-only checkpoint,
physical qualification remained open.


## External-loss software results and interrupted interface trial

Host and ASan/UBSan suites pass 62/62. Clean qualification and default Debug/Release
cross builds and ELF audits pass. RK3588 isolated-vcan passes with no prohibited TX.
Artifact SHA256: a8040dc1a7837a481644a00ce48293aed7cf09ccb9ac4b31255f7cb71372d3e7.
The frozen source archive SHA256 is
2f3a16a339675001bc5082d04b35498e249cd3f68a320cab8c4ea518c1b210d2.
Scoped clang-tidy exits 0 with advisory findings; the new path adds one conservative
const-return performance advisory. Its initial invocation failed on CMake module
response-file paths; the analysis-only compilation database removes those paths.
Production compile options and source are unchanged.

The first interface attempt sent one right +5 rpm target, then the interface-down
command failed with Operation not permitted. The stimulus was not applied; no
interface down/up result is accepted. Both final captures contain142 matching
frames. The executor handled cancellation and verified fresh zero speeds (all three
SDO views), dual 0x1421 final TPDO status, Pre-operational, and heartbeat/watchdog
baseline0 restoration. The operator confirmed both wheels stopped and site safe.
The coordinator's live count111 was cut short by its error stop; use the archived
captures and failure_analysis.json for final counts. The consumed runner was not
retried. Tests3 and4 remain unexecuted.

Read-only permission discovery also shows sudo requires a password for the exact
interface command. A future interface attempt requires an operator-accessible
privileged terminal and a new coordinated one-shot run. No sudoers or device/network
configuration has been changed.

The first vcan fixture failure treated expected socket loss as an error. The fixture
now reopens its own peer and monitor sockets only for the explicit interface case.
A subsequent run exposed false classification of a single SDO timeout as external
loss. The final implementation stops probing after that timeout and requires actual
feedback expiry or generation loss. The new missing-SDO/healthy-stream regression
passes along with loss, power boot, real interface down/up, absent loss, missing
recovery, first-zero timeout, SIGTERM and EMCY cases. Failure logs remain preserved.

The operator confirmed access to a local sudo terminal. The replacement
interface_loss_manual_once runner and interface_toggle_manual_once.py are staged
and hash-verified, but have not run. The root helper waits for an operator Enter
and the application's fresh motion-arm record; it performs only one can0 down,
3-second hold and up, with up attempted in finally. Four offline helper checks
(normal, interrupted hold, down failure, up failure) pass. No sudoers edit is needed.
The next step is operator terminal readiness, then one coordinated application run.

## Second coordinated interface attempt

The first manual coordinator request was not executed because automatic approval
review timed out. Its terminal gate expired; no motion or interface event occurred.
A new gate and runner, interface_loss_manual_v2_once, were staged and checked.
With operator terminal readiness confirmed, this new attempt ran once. The executor
reported qualification_expected_external_loss_absent with cleanup=verified.
Both captures contain301 identical frames. Packed zero was requested2913.009ms
after the single right+5rpm target. Final TPDO shows dual0x1421 and zero speed;
all three fresh SDO speed views are zero, with no subsequent Operational command.
The privileged helper exited without an interface event. Operator explanation and
physical acceptance are pending. This attempt does not qualify interface loss;
At that point, no retry or cable/power test had been executed.

The operator subsequently confirmed normal right-wheel motion and stopping, stated
that Enter was not pressed in time, and explicitly authorized one further attempt.
The new interface_loss_manual_v3_once entry preserves the same motion/loss bounds
and uses new one-shot markers. Its helper checks pass; hardware execution awaits
fresh local sudo terminal readiness. No result from v2 is relabeled as a loss pass.

## Third coordinated interface attempt — failed

The newly authorized interface_loss_manual_v3_once did execute down/up. The root
helper recorded 3001.582 ms from down completion to up start and successful up.
The coordinator exceeded its10000-frame limit during disconnection and canceled
the executor before recovery. The final archive has10624 JCAN frames, including
10380 identical TPDO1 frames (181#2714274400003300). Repeated retransmission without
an ACKing peer is a hypothesis; JCAN timestamps are zero and cannot prove timing.
The executor exited with qualification_owner_exit without verified cleanup.
A later read-only check confirms can0 UP/ERROR-ACTIVE and unchanged bitrate.
The operator reports both wheels normal and the terminal DONE. This does not
replace fresh protocol zero/state evidence. Test2 fails; tests3/4 remain unrun.
Do not retry this consumed runner. Resolve capture behavior and recovery cancellation
before another physical loss trial. Temporary drive settings remain unverified.

The operator explicitly authorized a new attempt with increased capture capacity.
interface_loss_manual_v4_once raises both coordinator and target candump limits
to100000 frames, retaining40s capture and unchanged TX/motion limits. Actual
validator boundary tests and helper restoration checks pass; scripts are staged.
The prior interrupted run's drive settings are not restored or freshly verified.
A drive-only power restoration and stopped-site confirmation are required before
this attempt; the normal application baseline checks remain mandatory. This is
preparation, not qualification of the fourth interface-loss test. No v4 motion started.

## Fourth coordinated interface attempt — 2026-09-14

The operator reconfirmed raised, stopped wheels, a safe site and terminal readiness.
The temporary target staging directory was absent; the original artifact and v4
scripts were restored and hash-verified before this single authorized execution.
The local capture-bound and four helper restoration checks passed again.

V4 is NOT QUALIFIED: the application reported expected external loss absent,
with cleanup verified. No privileged interface event was recorded. Both captures
contain 301 identical frames; one right +5 rpm request was followed by packed
zero after 2913.006 ms. Fresh SDO speed views 1/2/3 are zero, final TPDO has dual
0x1421 and zero speeds, and no later enable or Operational request appears.
JCAN configuration is unchanged. The operator confirmed normal stopping and site
safety, explaining that the motion was too short to notice before it ended.
See interface_loss_manual_v4_once/failure_analysis.json and operator_observation.json
under the existing evidence directory. The runner is consumed; no retry or longer
motion is authorized. At that point, cable/power-loss tests had not run. A future trial needs
reviewed operator coordination before another separately authorized execution.

## V5 extended operator window — 2026-09-14

The operator requested increased motion time and a new v5 test. The application
now uses an absolute eight-second deadline beginning before motion for external
loss only. Other stop/loss paths keep their previous intervals. V5 preserves the
same right +5 rpm, left zero, watchdog, three-second interface hold, recovery,
one-shot and capture limits; its terminal accepts a confirmed motion arm younger
than six seconds. Delayed-loss and absent-loss deadline regressions pass, as do
host 62/62, ASan/UBSan 62/62 and clean cross/ELF checks. See
[v5 preparation and evidence](evidence/p6_interface_v5_20260914/RESULT.md).
V5 subsequently executed once and passes with operator acceptance: down/up held
3003.580 ms, first resumed request packed zero, final dual 0x1460 and fresh three
zero speeds, heartbeat/watchdog baselines restored. Target 221 frames match in
order within JCAN 11525 frames; 11304 extra JCAN frames are identical TPDO1s,
whose timing/cause is unproven. No capture overflow occurred. Reopen emitted 299
buffer-initialization log lines, retained as a diagnostic follow-up. V5 is now
consumed; no retry or cable/power test has run. See the linked physical audit.

## Remaining stimuli — 2026-09-14

The operator requested continuation. The separate
[RK3588 CAN-branch cable-loss runner](evidence/p6_cable_loss_20260914/RESULT.md)
executed once against the unchanged v5 ELF and failed after controller errors
and diagnostic cancellation. Zero cleanup timed out; both runners are consumed.
145 target normal frames match in order within 6530 JCAN frames, with two target
controller error frames. The operator confirms stopping without restart, but
initially reported the drive remained powered, then confirmed power removal.
All hardware work stays paused for failure/recovery review; drive-only power
testing is blocked. Offline vcan tracing reproduced gate-denied SDO timeout
Abort attempts while the existing fixture still passes, exposing a diagnostic
coverage gap. Product code and the physical artifact remain unchanged.
The operator reports an emergency stop is connected, but no fault-injection
method is available. On 2026-09-14 the operator confirmed that this fixture has
no mechanical brake, so brake-output behavior is not applicable here. Emergency
input wiring/behavior and electrical bus-off/fault tests remain unverified.

## Drive-power-loss attempts — 2026-09-14

After the userspace inhibitor passed target and physical cable-loss validation,
a separate power-loss runner was prepared and staged on
the RK3588. It commands one right `+5 rpm` target with left zero, asks the
operator to remove only drive power, holds power off for at least three seconds,
restores power once for a five-second no-restart observation, and ends with drive
power off. It accepts either zero-first verified cleanup when the interface stays
usable, or fail-closed `can0` DOWN with no post-error RK3588 request. Both paths
require a post-motion drive boot in JCAN. It was subsequently authorized and
attempted once as recorded below.
See [the preparation](evidence/p6_power_loss_20260914/PREPARED.md).

The first authorized power-loss attempt is **INVALID**, not PASS. Its exact
timeline shows the application sent packed zero and inhibited `can0` before the
operator-confirmed power cut, so power loss did not trigger stopping. Silent
JCAN later exceeded the 100000-frame limit on repeated unacknowledged `0x701`
boot heartbeats. The safe evidence remains useful: one nonzero target, zero
post-error RK3588 requests, post-motion drive boot, operator-entered
`NO_RESTART` and final power-off, `can0` DOWN and no residual process. See
[attempt 1](evidence/p6_power_loss_20260914/RESULT.md).

[V2](evidence/p6_power_loss_v2_20260914/RESULT.md) was separately authorized and
executed once. JCAN serial `207F346D5650` ran in `normal --receive` mode, so its
controller actively supplied the CAN ACK bit while the runner submitted zero
JCAN data-frame commands. RK3588 and JCAN captured the same 352 frames. After
the restored drive boot, the first RK3588 request was packed zero 2.494 ms later;
no later nonzero target, NMT Operational or Enable Operation request occurred.
Cleanup reached raw dual status `0x14401440`, all three speed views were zero,
and the application exited 0. The operator accepted normal right-wheel stop,
stationary left wheel, no restart or abnormal sound, and final safe power-off.
The drive remains OFF and `can0` remains DOWN. The wrapper's false result is a
post-run assertion defect: it required an internal success context that the
successful executable does not print. Raw failure output is preserved; no
hardware retry was performed. Normal-mode ACK was active bus participation and
does not replace silent-observer evidence for cable-loss or inhibitor testing.

## September 14 powered-off cable repair

See [software repair and validation](P6_CABLE_LOSS_REPAIR.md). Host and sanitizer 62/62, commissioning 36/36, default Debug/Release 28/28 each and clean RK3588 cross/ELF pass. No target deployment or physical operation was performed. The failed cable trial remains failed; all consumed runners remain consumed. Reopen log repetition is repaired offline.
