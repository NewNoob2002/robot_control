# F1 zero-X1 recovery A2 — FAILED, feedback / incomplete restoration

Authorized by user: 改善阶段提示衔接，再进行 X1 恢复验证.
A1 remains FAILED/INCOMPLETE and is byte-preserved in preserved-a1.json.
Application SHA256 remains 2f97bdbafdb370e70ae47231609cf1ee698e56af2652f6e12e6c041970389058;
no application/safety/feedback-tolerance change or longer hardware window.

## Changed coordination

operator_console.py runs visibly in a dedicated local terminal. It shows the full
sequence and current fixture/powered readiness checklist before accepting START.
Only that explicit local confirmation creates fresh readiness and starts one
capture/application run. No chat-mediated phase timing. Cancel starts nothing.
run-recovery.py requires the visible terminal and current readiness. It displays
only the latest phase and a conservative countdown (approximately one second
shorter than the unchanged60s application window); expired, stopping or exited
snapshots suppress all action prompts. Target progress silence over3s after the
first phase aborts the trial. A terminal interruption requests target abort and
keeps independent capture alive through up to15s of bounded cleanup.

Actual control remains solely in RK3588, can0/node1/500000, UART586D017868;
JCAN207F346D5650 is silent. Full original zero-only limits,90s target guard,
12s cleanup grace,120s/20000frame capture and exact restoration remain in force.
Do not rerun consumed markers. After exit the console asks for physical OFF,
stationary wheels and no abnormal sound; protocol acceptance is audited separately.

## Verification

check_runner.py passes syntax, live phase selection, expiration/cleanup suppression,
archived A1 stale-prompt rejection and missing-readiness rejection before subprocess.
Target identity/hash/--help/pure ControlCycle smoke pass (stage-smoke.log).
JCAN identity/self-test/config-get match A1, with no configuration write.
Unchanged binary reuses the previously recorded43/43 Debug and43/43 sanitizer,
locked cross/ELF and virtual recovery evidence. No new physical acceptance yet.

Latest operator state is OFF after A2. Both one-shot markers are consumed.

## Actual disposition

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
