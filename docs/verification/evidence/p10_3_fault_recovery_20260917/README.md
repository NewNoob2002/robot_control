# P10.3 fault/recovery preparation — 2026-09-17

Latest: [F6 A1 PASS](motion_sigterm_a1/README.md), completing the planned bounded
unloaded F1–F6 HIL matrix. P10.3 scope/closure is in
[the checkpoint](../../P10_3_HIL_CHECKPOINT.md). Earlier OPEN/failed statements
below are historical checkpoints; their raw records remain unchanged.

Software verification complete; F1 zero-X1 A1 executed, FAILED / INCOMPLETE. User authorizes the
whole-chain fault/recovery work in [the plan](../../../plans/P10_3_FAULT_RECOVERY_HIL.md).
Latest operator confirmation is drive OFF after F1 A1. All144 existing
stop-regression evidence hashes are unchanged (preserved-evidence.json).
Original left/right negative-feedback failures remain failed; no tolerance changes.

## Implementation

Debug/default-OFF CLI adds --zero-x1-recovery and --zero-sbus-recovery. They observe
actual Reader/Source, ControlLoop and RuntimeSession; no synthesized authorization
or rewritten candidate. The independent send gate stays zero-only. The observer
requires actual stimulus/withdrawal held1s, nonneutral CH6 rejection, neutral
no-enable held1s, fresh source/system authorization, QuickStopActive→Disable Voltage
→new Disabled feedback→Shutdown→SwitchOn→Enable, then new zero enable held1s.
Incomplete scenarios, unexpected faults, nonzero feedback and overflow fail.
Stop cause7 means completed recovery. Production safety/runtime semantics are unchanged.

ControlQualification.prepare(true) requires current605A:00=5 before any write;
wrong option rejects before NMT/RPDO/SDO downloads. The object is never written.
Existing callers use the default contract. The independent recovery oracle checks
this baseline, real controlword/feedback order, dual captures, exact36 volatile
writes/restoration and rejects old-authority/missing-rearm/target/restore tampering.

## Actual verification

| Check | Result / evidence |
| --- | --- |
| Host Debug |43/43, no skips; debug-tests.log and debug-results.xml|
| Clang ASan/UBSan |43/43, no skips; sanitizer-tests.log/XML; ASAN_OPTIONS=detect_leaks=0, no LSan claim|
| Final narrow Debug recovery/oracle |PASS; recovery-oracle-final.log|
| Static and routing |scoped clang-tidy, clang-format, CI selector, actionlint and diff-check pass|
| Locked cross |101 steps; cross-build.py/log, real sysroot, immutable image, network disabled|
| ELF and source audit |ABI/dependencies/noRPATH pass;122 compiled source/project-header hashes match frozen snapshot|
| Target staging |identity/hash, --help and pure ControlCycle smoke pass; no CAN/UART opened|
| JCAN baseline |self-test/scan/config-get pass, configuration unchanged; no capture or CAN send|

Build directories are out/build/p103-control-hil and out/build/p103-control-hil-san.
Commands: cmake --build <directory> --parallel 2; ctest --test-dir <directory>
--output-on-failure --output-junit <XML>.
Static uses clang-tidy on main.cpp and control_qualification.cpp with
clang-analyzer/bugprone/performance/portability and warnings-as-errors; inherited
non-user warnings remain suppressed. CI requires the new recovery managed-vcan test.
No remote CI, clean release, hard-real-time, P6 soak or kernel-repair claim.

Eight virtual recovery cases: X1, failsafe, silence, missing stimulus, unexpected
diagnostic fault, nonzero command rejection, missing new rearm and wrong605A.
New moving X1/failsafe/silence cases complement existing movingSIGTERM and window/
feedback tests. Virtual X1/flags→zero bound100ms; silence injection→zero150ms
includes Source's100ms timeout. Hardware results cannot be inferred from these.

## Preserved failures / provenance

before-recovery.log records sandbox namespace rejection; escalated baseline fails
because the old executable lacks the CLI. First observer rejected the initial
Source stopped snapshot; second/third rejected the existing per-event zeroShutdown6
fallback. Those observer expectations were corrected, without changing runtime
policy. Original logs remain failed. Preparation quoting/patch errors started no
hardware. Local ninja dependency inspection warned about an old build-log version;
source/binary hashes were unaffected. Frozen dirty revision4c7390ff is attested in
cross-source.json.gz and cross-metadata.json.gz.

Artifact SHA256:2f97bdbafdb370e70ae47231609cf1ee698e56af2652f6e12e6c041970389058.
See [F1 readiness and sequence](zero_x1_a1/README.md). F1 A1 is consumed; see its incomplete-trial audit. F2/moving runners are not staged or executed;
UART silence requires an explicit physical method, not inference from RF failsafe.

## F1 A1 disposition

F1 A1 ran once after fresh readiness and remains FAILED / INCOMPLETE, with both
markers consumed. Operator reports only CH6; no X1 operation. Only fault_ready
was reached, about5.160s before the60s control deadline; no fault_observed or
recovery phases occurred. Application exited1 with control_hil_recovery_incomplete.
The late phase prompt was received together with exit and was not forwarded as
an instruction. This trial does not establish an X1 recovery defect or acceptance.

Offline audit matches8912 frames across both captures,6002 zero RPDOs, zero speed
feedback, inactive X1, healthy mode/fault, complete29485-row trace, and exact36
volatile writes/restoration.605A:00=5 preceded all writes. CAN errors/drops and
extended error counters did not increase. Normal recovery oracle remains FAILED;
--incomplete verifies only this incomplete trial's protocol/restoration.
Operator separately confirms both wheels stationary, no abnormal sound and drive
OFF. No retry or F2/moving trial started. Review phase coordination and obtain
fresh readiness before any new one-shot; P10.3 remains OPEN.

## F1 A2 coordination revision

User authorized another X1 recovery trial after fixing phase handoff. A2 uses a
local visible terminal, explicit START readiness, direct prompts/countdown and
stale/exit suppression with unchanged application and60s zero-only limits.
See [A2](zero_x1_a2/README.md); A1 remains consumed and FAILED/INCOMPLETE.

### F1 A2 executed — zero-target feedback failure

A2 was executed once after local terminal START. Live CONTROL_READY→FAULT_READY
handoff took3.895s, leaving about56s; the application stopped710.003ms after
fault_ready, before any X1 stimulus. Thus this is not the A1 manual-window timeout.
Only neutral CH1/CH3 and CH6 authorization occurred. All targets stayed zero;
left TPDO feedback was0.3/0.2/0.4rpm, right zero, X1 inactive, mode3/fault0.
The strict trace observer latched feedback_bad and stop cause6. Application exit1.

Target913 frames match the first913 of JCAN923; the10 extra independent tail
frames contain only zero-speed/healthy feedback and heartbeat. Complete2442-row
trace matches transmitted frames. First bad feedback→zero Shutdown6 was4.276ms;
SDO zero targets and Disable Voltage read back correctly. Cleanup then read
606C:01=0xffffffff (-1 signed, documented0.1rpm units), aborted before restoring
RPDO/TPDO2 maps, watchdog and heartbeat; only21 of36 expected writes occurred.
Stable zero appears149.978ms after the first bad frame and lasts900.157ms in the
remaining target capture, but this does not retroactively prove restoration.
CAN error/drop counters did not increase. Normal oracle remains FAILED; the
failure auditor rejects three target/trace/capture mutations. No retry occurred.

Operator confirms drive OFF, wheels visually stationary, no abnormal sound and
no X1 operation. Empty terminal post-trial input is retained separately from the
explicit chat follow-up. User requests a new zero-target feedback tolerance;
old criteria/results stay unchanged. Sensor-versus-physical origin is not proven.
Any future run must revalidate the full current baseline because cleanup failed.

### A4 causal review — 2026-09-17

A4 ran once, all targets zero, full baseline restored; operator confirmed stationary
wheels/no sound/OFF. After the first rearm the prompt stalled, so the operator made
another CH6 off/on. The first X1 recovery (source generation2) and later CH6 quick-stop
recovery (generation3) each have ordered feedback/commands and a one-second enabled
zero hold. The old observer incorrectly required QuickStopActive for native X1 and
borrowed the later event. Original PASS is retained but its combined causal claim is
superseded; the corrected oracle rejects that combined claim. See zero_x1_a4's
observed-sequences-analysis.json and README. P10.3 remains OPEN; corrected observer
hardware validation and remaining integrated fault/motion cases are pending.

### A5 corrected observer physical verification — 2026-09-17

A5 PASS within zero-target X1 scope with corrected616e8ad3 artifact: source/system
1→2 only; native-X1 Disabled path, nonneutral rearm rejection, first neutral rearm
and1.010s enabled hold independently verified. ZERO_REENABLED appeared on the local
terminal; application completion is recorded before cleanup.3406 dual-capture frames
match,2123 RPDOs all zero,36 volatile writes and exact baseline restoration verified.
Both feedback ranges0rpm with configured±1rpm tolerance;161.019/161.009ms standstill
holds. Trace10678 records complete, CAN errors/drop deltas0. Operator local OFF confirms
stationary wheels/no abnormal sound/power OFF. Both markers consumed. Timing retained:
3 missed periods and33.593ms maximum lateness; no hard-real-time claim. See
zero_x1_a5/README.md and recovery-analysis.json. Remaining moving/integrated fault
cases remain pending; P10.3 stays OPEN. A4 original evidence unchanged.

### F2 A1 — zero-target SBUS timeout recovery accepted (2026-09-17)

Unchanged616e8ad3 completed one F2 trial. Actual fault:10.660s valid-frame gap and
100.020ms timeout withdrawal; all6648 received valid frames have flags0.51 raw bytes
occur inside the gap, so electrical UART silence/F5 is not claimed. Nonneutral rejection,
neutral fresh1→2 rearm and complete quick-stop/Disable Voltage/new Disabled/enable order
pass the independent oracle.8586 matching frames,5721 zero RPDOs,36 writes/full baseline
restoration, complete26675-record trace. Operator local OFF confirms stationary/no sound.
Markers consumed; flags4/12 failsafe and moving scenarios remain pending; P10.3 OPEN.
See zero_sbus_a1/README.md and recovery-analysis.json in fault-recovery evidence.

### F2 A2 accepted; A1 stimulus corrected — 2026-09-17

User confirms A1 switched receiver power while transmitter stayed ON: prior A1 timeout
recovery evidence remains valid, but transmitter-loss attribution is superseded.
Original A1 files remain unchanged; correction is archived in zero_sbus_a2.
A2 switches only the handheld transmitter; operator confirms receiver continuously powered.
Valid-frame flags0→4→12→0, max gap10.920499ms; actual frame_lost then failsafe recorded.
Independent oracle verifies nonneutral rejection and fresh neutral quick-stop recovery.
4010 matching dual-capture frames,2542 zero RPDOs,36 writes/full restoration, app exit0.
Operator OFF confirms stationary/no abnormal sound. Both markers consumed. F2 zero-target
transmitter-loss recovery accepted; no isolated failsafe-only, moving or F5 claim.
See zero_sbus_a2/README.md and recovery-analysis.json. P10.3 remains OPEN.

### F3 A1/A2 and single-operator mode — 2026-09-17

A1 never moved: initial throttle produced bilateral3rpm candidates rejected before send;
1882 matching frames,1121 zero RPDOs, full restoration. User requested single-person input.
New Debug-only --right-throttle projects only left command to zero before arbitration,
retaining Source/raw/authority; steering must stay neutral, reverse input rejects.
Debug/sanitizer43/43, static/cross/ELF and target smoke pass; artifact778b0a79.
A2 actual right3..5rpm motion worked, but cause2 automatic cutoff preceded X1 by260.791ms
on the application clock. Five post-stop negative feedbacks (min-1.1rpm) retain FAILED.
1716 matching frames, full36-write restoration, stable zero verified561.985ms; operator
OFF/no sound/left stationary confirmed. Both A1/A2 markers consumed. F3 remains INCOMPLETE,
P10.3 OPEN. See motion_x1_a1, motion_x1_a2 and right_throttle_preparation in fault evidence.

### 单轮运动验收收口：CLOSED；反馈判据修订为 ±1.5rpm

用户确认停止时负反馈来自驱动器内部速度估算，按正常波动记录，并明确关闭该保留项。
左右轮方向、实际停止、对侧静止及无异响已有现场确认；两轮 A1 原始轨迹按±1.5rpm复核通过，
见 standstill_15_revision/single-wheel-revised-audit.json。旧抓包及原判据失败保留，新增修订结论；
历史−3.6rpm样本不属于新阈值数值范围，不将其重写为±1.5rpm内。
后续 HIL 统一使用 --zero-feedback-tenths-rpm 15（控制模式默认15），包含运动/停车与清理。
原始速度不改写，非零目标、单轮限速/3秒上限不变，明显反向超阈值仍拒绝；>=150ms稳定观察保留。
F3运动中X1时序仍须独立通过，不能由单轮收口或容差替代。当前新F3 A3准备重跑。

### F3 A3 — moving X1 PASS under revised ±1.5rpm criterion

User-authorized rerun uses3652f15b --right-throttle --zero-feedback-tenths-rpm15.
Debug/sanitizer43/43, static/cross/ELF and target smoke passed. One actual right-wheel
run: X1 observed at1.848s before cutoff; application receive→zero-send32.084us,
no later nonzero command; stable feedback within±1.5rpm verified299.740ms after first zero.
Post-stop raw feedback−0.5..+0.2rpm retained/classified normal in approved band.1433 dual
frames match,746 RPDOs,36 writes/full restoration, complete4178-record trace with no
feedback failure. Exit1 is expected X1 revocation, verified independently. Operator
OFF/right direction+stop normal/left stationary/no sound/X1 locked confirmed. A3 markers
consumed; no next test started. F3 accepted; F4/F5/F6 remain pending, P10.3 OPEN.
See motion_x1_a3/README.md in fault-recovery evidence. Single-wheel acceptance CLOSED.
