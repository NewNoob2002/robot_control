# P6.4 Single-process Zero-target Executor Baseline

Date: 2026-09-07. **P6.4 COMPLETE; P6.5 is next.**

The dated OPEN statements below preserve the state of earlier attempts. Final
acceptance is recorded in the last sections of this document.

## Implemented behavior

CLI: robot-control-zlac-qualification --interface can0 --zero-sequence

The existing single lifecycle owner waits up to 180 seconds for actual boot-up
and current heartbeat. Ordinary heartbeat before boot does not satisfy the
gate. No simulated boot, NMT reset, heartbeat configuration or nonzero command
was added. Other primitive CLI operations retain their original startup bound.

The sequence locks boot/transport generation and uploads mode request/display,
both targets, fault value and all three velocity values. It requires mode 3,
zero targets/faults/velocity before any download. It verifies both zero target
writes, requests Operational and waits for a newer matching heartbeat, verifies
mode 3, then issues Shutdown/Switch On/Enable Operation. Each step requires a
newer TPDO1 with BOTH neutral status halves matching and packed velocity zero,
using the existing CiA402 decoder and TransitionTracker. Independent/packed
zero velocities are uploaded after each reached state. Physical axis mapping
is not inferred.

Normal cleanup verifies both target zeros, both Shutdown states, zero velocities
and a newer Pre-operational heartbeat. On active failure, the existing bounded
zero/Shutdown/Pre-operational cleanup is attempted; the primary failure records
whether cleanup was submitted without terminal verification or itself failed.
A failed initial read-only preflight sends no downloads. Completion or failure
permanently inhibits another sequence. The final Pre-operational request is
attempted even if normal cleanup state verification fails. There are no retries.

Generation, heartbeat, EMCY/CAN-error and malformed-frame checks also run during
foreground SDO processing. Fresh nonzero TPDO velocity aborts the zero sequence.
Process exit does not establish a wire-level stop.

Bounds: SDO 500 ms; state/NMT transition 2000 ms; heartbeat freshness 1000 ms;
TPDO freshness 100 ms. Normal path: 9 downloads, 25 uploads, 2 node-specific NMT
requests. The unchanged 100 ms TPDO limit still needs real-jitter qualification
against the drive's historically recorded 100 ms event timer.

## Verification

Evidence directory: evidence/p6_4_20260907_software/.

| Check | Result |
|---|---|
| New test before implementation | Link failure on missing sequence definition, then passed after implementation |
| GCC Debug qualification CTest including managed vcan | 39/39 PASS |
| LLVM 22.1.8 ASan/UBSan qualification CTest including managed vcan | 39/39 PASS |
| Concurrent independent vcan capture | 582 frames; zero prohibited frames; zero failures |
| Normal Debug/Release builds and non-network CTest | PASS; 25/25 each |
| P5.6 build and non-network CTest | PASS; 31/31 |
| Default/P5.6 artifact isolation | Qualification symbols absent in three checked artifacts |
| LLVM formatting and warnings-as-errors | PASS; existing log wrapper format attribute corrected |
| clang-tidy analyzer/bugprone scope | Completed; four reviewed warnings below |
| Dependency pin and target sysroot checks | PASS |
| Locked Docker aarch64 Debug build | PASS |
| Qualification ELF architecture/dependencies/versions | PASS |
| Deployment, target smoke and active zero-target HIL | PASS; final evidence below |

New vcan scenarios cover normal cleanup, unequal dual states, nonzero packed
velocity, boot-generation change, missing new TPDO, SIGTERM, EMCY, failed
failure-cleanup transfer, failed normal-cleanup state verification, wrong
initial mode, invalid timeout and inhibited re-entry. Existing P6.3 scenarios
cover SDO timeout/late response, target readback mismatch, stale feedback and
heartbeat, signal cancellation and interface loss. This is not a claim of a
new full-sequence CAN-error/link-reopen matrix or physical fault injection.
The capture thread inherits blocked termination signals so the owner signalfd
receives SIGTERM instead of the test capture thread terminating the process.

clang-tidy reported three existing optional-access warnings in the generic
Result wrapper and one convertible-parameter warning in the test TPDO builder.
Changed callers check Result::ok() before value(); test arguments are explicit.
No analyzer defect was found in the new sequence. No global suppression added.

The original ELF audit requires platform-probe symbols such as ppoll and is not
applicable unchanged. The preserved audit_qualification_elf.sh reuses its ABI,
dependency, RPATH and symbol-version checks with qualification-specific symbols.
The original failed invocation remains in cross_debug.log; elf_audit.log records
the applicable PASS. Production audit behavior is unchanged.

Initial sandboxed socket tests failed on denied socket/netlink operations;
qualification suites subsequently passed outside the sandbox. A later repeat
of all ordinary/P5.6 network tests did not start due to automatic approval
timeout. Those later network regressions are not claimed passed.

## Reviewable artifact

Source HEAD: 1d1314d25269d38f9679cd2c15e23df65afcd2a4, dirty; existing work preserved.
The evidence directory includes source_checksums.json and artifact_isolation.json.
Toolchain image: sha256:f2198e31e27c084bc2deff761e124fa9d7ce580a8d986c7885fd62bb1701e7dd.
Actual Ubuntu 22.04 target sysroot was validated against its lock.
ELF SHA-256: ac24cc41ccb2684c0464eac82ea5388ff1fc0ceffa8cef9cc23f17eec770c971.
Local package: out/staging/p64-ac24cc41ccb2/, with SHA256SUMS and manifest.
Proposed destination: /opt/robot-control/qualifications/p64-ac24cc41ccb2/.
Not deployed; no production service or persistent configuration changed.

## Physical preflight and blocker

User authorization for P6.4 writes and confirmation of operator presence,
powered drive, raised wheels and independent power-disconnect stop remain valid.
Target identity: lubancat, machine-id 6923ab3301fb4a8d816759b04ec6bf0a.
Live can0: UP/LOWER_UP, ERROR-ACTIVE, Classical CAN 500000, restart-ms 100,
zero errors, RX 1, TX 0. RK3588 and JCAN each received no frames during bounded
5-second passive windows. Silence does not prove a specific setting or fault.

JCAN serial 207F346D5650 and unchanged raw configuration were archived. The MCP
profile is not enforced and is not a blocker. An exact 0x1017:00 upload was
prepared to investigate missing heartbeat. Automatic approval timed out; a retry
was interrupted, and a later continuation timed out at approval. No SDO result
or new runtime SDO evidence file was produced. The overlapping target capture
was empty. These are NOT drive SDO timeout results: execution/transmission is
not positively established. Do not silently resend an unresolved request.
The later periodic-list query also timed out at approval, so no new periodic
state is claimed.

Continuation: another read-only JCAN scan returned an automatic approval
deadline error without device evidence. SSH inspection succeeded and reconfirmed
the same machine ID, can0 ERROR-ACTIVE at 500000 bit/s, RX 1, TX 0 and zero
error counters. Both /opt/robot-control and its qualifications directory are
absent. The staged ELF checksum was revalidated; no deployment or active CAN
operation was started during this continuation.

Historical notes record volatile 0x1017=1000 producing about 500 ms heartbeat,
then restoration to zero. This is not a current readback. Working tool approval,
current heartbeat verification, a reviewed real-boot procedure with the operator,
and current identity/fixture checks are required before physical enabling.
Do not weaken boot validation or introduce heartbeat writes/reset implicitly.
Independent overlapping captures must cover execution and cleanup. If cleanup
cannot be verified, the operator must use independent power removal.

Phase 6 remains in progress: P6.4 HIL, first motion, stop/communication-loss
qualification, axis mapping and final acceptance are not completed by this work.

## Tool recovery continuation — 2026-09-07 02:49 UTC

JCAN MCP scan and get_config succeeded for serial 207F346D5650 without
warnings. The raw configuration was recorded again. periodic_list returned
no active or recent tasks and no session cleanup error. A bounded 5000 ms
listen-only capture completed in 5002.793 ms with zero received frames and
zero dropped frames. Current heartbeat and actual boot-up remain unproven;
this quiet window does not establish a drive fault or heartbeat setting.
No SDO was resent, no configuration or physical TX was requested, and no
deployment or active HIL was performed in this continuation. Existing hardware
authorization is retained; the real-boot procedure still needs operator
coordination before overlapping captures and executor startup.

MCP evidence (under /home/gtc/Desktop/workspace/JCAN/evidence/runtime/):

- 20260907T024857.825722Z-1788749337825778332-scan.json
- 20260907T024909.260710Z-1788749349260737339-get_config.json
- 20260907T024924.280093Z-1788749364280113893-periodic_list.json
- 20260907T024942.129159Z-1788749382129197558-capture.json

## Operator-coordinated boot preparation — 2026-09-07 02:56 UTC

The user agreed to manually power-cycle the drive to produce a real boot-up.
The operator was told to wait for capture readiness. SSH rechecked the same
machine identity and can0 ERROR-ACTIVE at 500000 bit/s, RX 1, TX 0 and zero
errors. The first passive candump invocation rejected combined `-L -e`
options; its diagnostic is preserved under evidence/p6_4_20260907_boot_capture/.
A second invocation used `timeout 65s candump -ta -e can0,0:0,#FFFFFFFF`.
JCAN rejected the requested 60000 ms capture with `ok=false`: its accepted
duration range is 1..5000 ms. This is tool-argument validation, not approval
failure or a drive fault. Evidence:
/home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260907T025636.843647Z-1788749796843677771-capture.json.
The stop condition was honored without retry; the operator was told to defer
power-on. No deployment, drive write, executor run or successful dual capture
is claimed. Continuous independent capture covering startup, sequence and
cleanup needs a supported bounded MCP capture window before active HIL.

## Long-capture recheck — 2026-09-07 03:11 UTC

After the user reported raising the MCP source limits and adding cancellation
cleanup, the connected service was checked through MCP only. Scan found
207F346D5650 and get_config returned the same raw baseline without warnings.
The requested 60000 ms / 100000 frame passive capture still returned
`ok=false`, `duration_ms 范围必须是 1..5000`. The connected service therefore
has not demonstrated the updated limit; no long hardware capture occurred.
No callable MCP service-restart interface was available in this session.
No service was killed or launched directly, and no physical operation was
retried after the rejection. Reload/reconnect is required before revalidation.
No deployment or drive TX was performed.

Evidence under /home/gtc/Desktop/workspace/JCAN/evidence/runtime/:

- 20260907T031120.525352Z-1788750680525375131-scan.json
- 20260907T031136.885471Z-1788750696885492151-get_config.json
- 20260907T031157.569925Z-1788750717569947994-capture.json

## Long-capture recovery — 2026-09-07 03:18 UTC

The current MCP service completed a bounded 60000 ms / 100000-frame
listen-only capture on serial 207F346D5650: actual duration 60000.651 ms,
zero received/matched/dropped frames, ok=true and no warnings. The former
5000 ms service limit is resolved for this tested window. This proves capture
availability, not boot/heartbeat reception or active HIL acceptance.

Scan and baseline configuration succeeded; periodic_list reported no active
or recent tasks and no cleanup error. Post-capture configuration matched the
baseline. SSH reconfirmed lubancat and machine ID
6923ab3301fb4a8d816759b04ec6bf0a; can0 was UP/LOWER_UP, ERROR-ACTIVE,
500000 bit/s, restart-ms 100, RX 1, TX 0, with zero error counters. No matching
robot-control/candump/cansend process was found (pgrep exit 1). The staged
qualification ELF still matches ac24cc41ccb2684c0464eac82ea5388ff1fc0ceffa8cef9cc23f17eec770c971.

Raw MCP evidence is copied to evidence/p6_4_20260907_long_capture/.
No SDO, drive write, interface change, deployment or power-cycle occurred.
The next gate is operator-coordinated actual boot under overlapping captures;
existing authorization is retained, but the operator must wait for explicit
capture readiness before power-on. Current identity/values and fixture
readiness remain required before enabling. P6.4 physical acceptance is OPEN.

## Coordinated actual boot and heartbeat read — 2026-09-07 03:25 UTC

The operator agreed to wait for notification, then confirmed manual power-on.
RK3588 candump was confirmed running before notification; the JCAN capture
call was pending. Final evidence establishes both observers received the same
standard boot frame, 0x701 DLC 1 data 00. RK3588 recorded it at
1788751386.039582; JCAN reports timestamp 0, so no cross-clock timing claim
is made. The 120001.513 ms JCAN window received exactly one frame, dropped
zero, and completed without warnings. The 150-second target capture likewise
contains only that boot frame and ended with expected timeout exit 124.
No subsequent heartbeat or TPDO was observed.

Under the existing bounded heartbeat-read preflight authorization, one new
JCAN 0x1017:00 expedited upload completed successfully with no retry. Earlier
approval-blocked attempts remain unresolved execution records, not drive
timeouts. Request: 0x601 / 40 17 10 00 00 00 00 00. Response:
0x581 / 4B 17 10 00 00 00 00 00, returning U16 zero. A separate bounded
30-second RK3588 capture contains the exact request and response at
1788751510.379322 and 1788751510.379642. Its exit 124 is the capture deadline.
This is a current heartbeat-production setting readback; no write occurred.
The historical ordinary-heartbeat readiness blocker remains, now supported
by a live zero setting rather than an assumed historical value.

Artifacts: evidence/p6_4_20260907_boot_capture_2/ (raw captures, MCP records,
before/after target counters and inherited read-preflight scope). Adapter
configuration is unchanged. No deployment, enable, drive download, NMT or
nonzero target was sent. The passive boot capture does not satisfy boot
generation inside a future newly launched qualification process.

Next proposed scope extension, NOT AUTHORIZED OR EXECUTED: on node 1,
temporarily write 0x1017:00 U16 1000, read it back, observe heartbeat for
10 seconds, restore the recorded U16 zero and read back. At most two
downloads and two uploads, no retries, no persistent store or enable;
stop and preserve evidence on anomalies, with operator power removal if
restoration cannot be verified. Exact downloads on standard 0x601 DLC 8:
2B 17 10 00 E8 03 00 00 and 2B 17 10 00 00 00 00 00. This parameter is
outside the previously approved P6.4 fixed download list and needs explicit
scope authorization before use. Future active HIL still requires a reviewed
single-owner startup procedure and all remaining readiness gates.

## Authorized heartbeat trial — 2026-09-07 03:31–03:34 UTC

The user explicitly authorized the preceding bounded 0x1017 configuration
trial. Evidence and safety_preflight are under
evidence/p6_4_20260907_heartbeat_trial/. One JCAN send_once requested
standard 0x601 DLC8 2B 17 10 00 E8 03 00 00 and returned ok=true without
warnings. The single following SDO upload returned U16 zero, not 1000:
0x581 / 4B 17 10 00 00 00 00 00. The readback mismatch failed the trial.
The 10-second observation and further writes were stopped; no retry occurred.
Because the readback already equals the recorded baseline zero, no redundant
restoration download was sent. No enable, NMT, persistent store or deployment
occurred.

The independent 180-second RK3588 capture contains only the exact upload
request and response, with no recorded download or download acknowledgement.
However, target RX increased from 4 to 7 packets and 18 to 42 bytes, while
the raw capture accounts for only two eight-byte frames. One received frame
is unaccounted for; complete capture coverage is NOT established. The MCP
success cannot establish that the drive received/applied the write, and the
missing raw frame cannot prove no write reached the wire. No specific driver,
adapter or drive root cause is asserted.

Target CAN remained ERROR-ACTIVE with TX zero and zero errors/drops. The
capture ended at its deadline (exit 124), no candump/cansend process remained,
and postflight JCAN raw configuration matched baseline. test_result.yaml
records failure and the evidence gap. P6.4 stays OPEN; diagnose delivery and
capture readiness before proposing any new physical attempt.

## Explicit receiver readiness trial — 2026-09-07 03:43 UTC

The user explicitly requested a new write after confirming reception ready.
Evidence is in evidence/p6_4_20260907_heartbeat_ready_trial/. Before starting,
can0 raw all/error receive lists were empty. Before sending, candump PID 5367
was running with line-buffered output and both can0 receive registrations
were present (receiver_ready.txt). One standard 0x601 request
2B 17 10 00 E8 03 00 00 returned MCP ok=true. No write confirmation or
abort was captured. At 03:44:03 UTC the same capture process remained live,
RX had advanced 7->8 packets / 42->50 bytes, but raw matches remained zero.

One diagnostic/final-value upload then returned 0x1017 U16 zero. The same
capture received its exact request and response; raw matches advanced to 2,
RX to 10 packets / 66 bytes, and error matches stayed zero. This attempt
therefore does not support explaining the missing frame solely by capture
startup lateness. The unmatched eight-byte RX increment does not identify
the frame format or establish what reached the drive. No specific MCP,
kernel-driver or drive root cause is established.

The experiment failed and no automatic resend, enable or NMT occurred.
Final readback equals baseline zero, so no redundant restoration write was
sent. Adapter configuration is unchanged; target TX and errors remain zero.
The next investigation should compare send_once versus the working SDO-read
transmit path and explain the RX/raw-delivery discrepancy before another
physical attempt. P6.4 remains OPEN.

## Explicit send-then-heartbeat observation — 2026-09-07 03:54 UTC

The user requested another configuration transmission followed by checking
0x700+node heartbeat/status. Evidence and authorization are recorded in
evidence/p6_4_20260907_heartbeat_observe_trial/. RK3588 candump PID 6026 and
can0 raw all/error receive registrations were confirmed before the single
0x601 / 2B 17 10 00 E8 03 00 00 request. MCP send_once returned ok=true.
The subsequent JCAN 10000.601 ms listen-only capture received zero frames,
with zero drops and no warnings. RK3588 likewise recorded no heartbeat,
download acknowledgement or abort. No state byte or interval can be decoded.

One final SDO upload returned U16 zero; its request and response were both
captured by RK3588. RX advanced 10->13 packets / 66->90 bytes, while raw
matches were 2 and error matches zero. The same unmatched eight-byte RX
increment remains unexplained. Adapter configuration was unchanged; target
TX and errors/drops stayed zero. The current baseline zero was verified, so
no redundant restoration download was sent. No retry, NMT or enable occurred.
The requested observation failed; P6.4 remains OPEN.

## Reported MCP repair retest preparation — 2026-09-07 06:32 UTC

The operator reported that direct RK3588 cansend with the same payload
produced 0x701/7F at approximately 499 ms, and later reported a JCAN MCP
repair and authorized retesting. The current tool exposes bounded settle_ms
before CANStop; its implementation fix has not been independently inspected.
Scan/config succeeded, but SSH to robot-dev returned No route to host, with
neighbor state FAILED. A 10-second JCAN passive capture received zero frames.
No active transmission was attempted without independent target reception.
The latest drive value is unknown; do not reuse the earlier zero baseline.
See evidence/p6_4_20260907_jcan_fix_retest/result.md and raw MCP evidence.
Restore target connectivity and establish current state before active retest.

## Repaired JCAN bounded send retest PASS — 2026-09-07 06:37 UTC

Following the operator's reconfirmation of power and connectivity, the target
identity and ERROR-ACTIVE 500000 bit/s interface were verified. A new
line-buffered candump and its raw all/error receive registrations were
confirmed before transmission. Baseline 0x1017 uploaded U16 zero; the earlier
operator-set 1000 was not assumed current.

One JCAN send_once of 0x601 / 2B 17 10 00 E8 03 00 00 with settle_ms=500
was captured exactly at 1788763029.683619. The drive replied with
0x581 / 60 17 10 00 00 00 00 00 at 1788763029.683965. Subsequent upload
returned U16 1000. JCAN's 10000.430 ms passive capture received 20 standard
0x701 / 7F frames, zero drops and no warnings. RK3588 recorded 147 heartbeats
over the full enabled interval, spaced 498.976–499.046 ms (mean 499.025 ms).
JCAN sample timestamps are zero, so cadence comes from RK3588 timestamps.

The authorized baseline restoration request 2B 17 10 00 00 00 00 00 used
the same settle_ms=500, received the exact download acknowledgement, and
uploaded U16 zero. No automatic retry, NMT, enable or persistent store occurred.
Adapter configuration remained unchanged. RK3588 RX increased by exactly
157 frames / 227 bytes, matching the complete 157-frame raw capture; target
TX remained at the pre-existing 2 frames, with zero errors/drops. The bounded
capture finished with expected timeout exit 124; the operator's pre-existing
candump was left untouched.

Evidence: evidence/p6_4_20260907_jcan_fix_live/, including safety_preflight,
test_result, analysis, exact MCP records, raw capture and before/after counters.
This passes the repaired-service send/acknowledgement/readback/heartbeat and
restoration trial with a 500 ms settle. It does not prove default-zero settle
works or identify the exact implementation root cause. The earlier failures
remain recorded. Full P6.4 zero-target CiA402 HIL acceptance remains OPEN.

## Remaining-test preparation and preactivation HIL — 2026-09-07 07:03 UTC

Current source checksums for the six recorded P6.4 implementation/test files
match the software-verified baseline, and the ELF checksum is unchanged.
The verified aarch64 artifact was staged at
/tmp/robot-control-qualifications/p64-ac24cc41ccb2/ on the identified RK3588.
/opt staging was unavailable without sudo credentials; no production service
or existing deployment was changed. Target SHA256 verification passed and
no-argument CLI loading returned the expected usage/exit 2 before CAN creation.
The target lacks file; this was recorded, not treated as an ELF mismatch.

Nineteen fixed current uploads succeeded without warnings and have all 38
request/response frames in RK3588 capture. Identity/version values match P6.1.
Mode request/display are 3; both targets, all three velocity values and fault
value are zero; raw status is 0x14001400, controlword and heartbeat setting
zero, 0x2000 zero and 0x200F one. These are current preflight values, not proof
of physical brake behavior or post-boot values in a future active run.

Two target preactivation cases passed their narrow oracles:

- Missing boot/current heartbeat: wait_ready timed out after 60 seconds,
  executor exit 1, no state sequence or physical TX. A 100-second independent
  JCAN window captured zero frames. The target raw capture ended before the
  executor did; complete dual raw coverage is not claimed for this case.
- SIGTERM while waiting: canceled at 3 seconds, emitted
  qualification_owner_exit/Operation canceled, wrapper exit 124, no orphan
  executor, and zero frames on both observers. This does not prove termination
  cleanup after enabling.

Both startup logs include one CO_CANsend Permission denied. Source review
shows the qualification gate returns EACCES before send when no foreground
authorization exists; target counters and independent captures confirm zero
physical TX. No gate was weakened and no precise internal frame is asserted.
Target TX stayed at its pre-existing two frames; RX increased exactly by the
38 read frames. Errors/drops remained zero. All test processes completed.

Evidence: evidence/p6_4_20260907_remaining_preflight/, including fixed raw
reads, deployment verification, separate test results and startup logs.
Normal zero-target transitions and post-enable mismatch/staleness/timeout/
EMCY/termination cleanup remain unqualified on hardware. Before active
enabling, the operator must complete the pending unloaded/wiring/brake fixture
declaration and coordinate a real boot after capture and executor readiness.
Existing write authorization is retained; no drive download or enable was
performed in this continuation.

## Operator-window rebuild and deployment — 2026-09-07 08:04 UTC

The zero-sequence startup bound was changed from 60 to 180 seconds so the
operator can power the drive after the target-side `READY_TO_POWER_ON` marker.
Primitive operations retain their one-second bound. Host qualification build,
11/11 qualification CTest, the locked no-network RK3588 cross build and ELF
audit passed. Source snapshot SHA-256 is
`0f5ba8c5f199d7048617b451b944b81a18caeb0b84eab30e9ef7a3aa6cd66bc7`;
the deployed ELF SHA-256 is
`0bb8aa097a0fe1e1e6ed59bcedeb866f011403988219b7e2c453c03480cc95da`.
It was staged only under
`/tmp/robot-control-qualifications/p64-180-0bb8aa097a0f/`. Evidence is in
`evidence/p6_4_20260907_active_sigterm/`.

## Normal zero-target HIL PASS — 2026-09-07 07:31 UTC

Evidence: `evidence/p6_4_20260907_zero_sequence_2/`. A real `0x701/00` boot,
the acknowledged temporary heartbeat setup, and the complete owner sequence
were captured. The executor issued exactly 9 downloads, 25 uploads and two
node-specific NMT commands. Both status halves transitioned
`0x1421 -> 0x1423 -> 0x1427 -> 0x1421` in 20.780, 41.784, 42.680 and
33.642 ms. TPDO1 intervals were 49.942..49.956 ms. Every target and recorded
velocity was zero. JCAN's 260-frame capture matches the RK3588 prefix exactly
with zero drops. The user confirmed both wheels never moved and the installed
mechanical brakes made no abnormal movement or sound. Heartbeat `0x1017` was
restored to zero and read back.

The preceding `zero_sequence_1` attempt remains failure evidence: boot arrived
near the former startup deadline and no active sequence command was sent. A
retry is not hidden by the passing run.

## Post-enable SIGTERM HIL PASS — 2026-09-07 08:18 UTC

Evidence: `evidence/p6_4_20260907_active_sigterm_180/`. The helper sent one
SIGTERM to the exact owner PID only after the exact dual-enabled, zero-velocity
TPDO1 `0x181 / 27 14 27 14 00 00 00 00`. The executor reported
`qualification_owner_exit: ... Operation canceled`, sent no later re-enable,
verified zero targets and velocities, sent Shutdown `0x0006` 16.189 ms after
the enabled TPDO, and sent node-1 NMT Pre-operational 28.678 ms after it.

The target capture contains 278 frames. JCAN independently captured an exact
233-frame prefix with zero drops. The main capture has 33 correlated SDO pairs,
no SDO abort and no CAN error frame. Final independent uploads returned
`0x1017=0`, `0x6040=0x0006`, dual `0x6041=0x1421/0x1421`, mode request/display
3, both targets zero, all three velocity values zero and `0x603F=0`. The user
again confirmed no wheel motion and no abnormal brake action or sound.

Cleanup sent NMT Pre-operational 12.489 ms after Shutdown, before a newer TPDO1,
so immediate Shutdown-state timing is not claimed; the terminal dual Shutdown
state is established by the later SDO readback. JCAN covers the exact main
prefix while RK3588 covers the later heartbeat restoration. A verified orphan
`candump -L can0` PID 14690 was terminated without touching other processes.
Final `can0` state was ERROR-ACTIVE at 500000 bit/s with zero errors/drops; JCAN
configuration was unchanged and no periodic task remained.

Four `CO_epoll_processLast` messages in this run and seven in the normal run are
vendor `LOG_DEBUG` receive diagnostics, not CAN fault evidence. Startup
`CO_CANsend Permission denied` is the qualification gate rejecting unauthorized
internal stack transmission before `send()`; target counters and both captures
show no corresponding physical frame.

## P6.4 acceptance

P6.4 combines real-drive normal and post-enable termination HIL with the
managed-`vcan` negative matrix. The latter proves unequal dual status, nonzero
velocity, stale/missing TPDO, SDO timeout and late response, EMCY
(`0x081 / 01 00 01 00 00 00 00 00`), SIGTERM and cleanup failures enter inhibit
and bounded cleanup without re-enable.

No synthetic EMCY or conflicting node-1 TPDO was injected on the physical bus:
that would only repeat the already-tested observer path and would not qualify
drive fault behavior. Heartbeat/TPDO staleness and controlled interface loss
remain P6.6 stimuli as assigned by the phase plan. This evidence split satisfies
the P6.4 zero-target acceptance without claiming physical drive-fault behavior.
No nonzero target, fault reset, brake-output write, reset or persistent store
occurred. P6.4 is complete; P6.5 one-axis first-motion semantics is next and
requires a new exact authorization and preflight.
