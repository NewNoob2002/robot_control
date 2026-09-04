# Phase 5 CANopen Integration Baseline

Status: **COMPLETE — P5.7 TARGET RX, ZERO-TX, REGRESSION, AND EVIDENCE PASS**

Source revision: 0d32ca928e6c3969bd8a1007870a10e1b7e5cb27

Verification date: 2026-09-04

## Result

P5.1 through P5.7 are complete. The P5.7 local, managed-vcan,
sanitizer, static-analysis, RK3588 cross-build, target deployment, bounded
deadline, SIGTERM, cleanup, and zero-transmit subgates pass.

The passive target windows contained no heartbeat, EMCY, or TPDO traffic.
Across three separately authorized active requests, JCAN sent the same exact
node-1 read-only SDO upload for 0x2008:00 and received the same drive response:
0x581 with payload 4B 08 20 00 C8 00 00 00. RK3588 can0 RX advanced from zero
to six cumulative frames while TX remained zero.

The first two observer windows did not publish the response. The resumed
diagnostic then proved both an unfiltered target raw socket and the normal
observer's eight exact filters, including 0x581, were installed before invoking
the third upload. Both target logs remained empty and the observer snapshot
stayed at version 1. However, the target window ended at 02:49:01 UTC while the
JCAN success evidence was not written until 02:49:28 UTC and does not expose
the physical transmit timestamp. This run therefore does not distinguish a
late stimulus from a target kernel/driver SocketCAN delivery difference. No
further CAN transmission was inherited from that run.

A newly authorized immediate `jcan_send_once` then removed the timing
ambiguity. Its success record at 03:13:08 UTC fell inside a target window from
03:12:48 through 03:13:49 UTC. While both raw and filtered sockets were still
registered, can0 RX/RXF advanced from six to seven but the unfiltered socket and
all exact filters retained zero matches. The raw log stayed empty and target TX
remained zero. A later operator control changes the interpretation of this
result: 50 ms 0x7FF traffic was received normally by `candump`, so this does not
establish a general RK3588 kernel/BSP SocketCAN delivery failure. The remaining
difference is specific to the JCAN single-shot/0x601 case or its frame
classification. These failed attempts remain preserved as diagnostic history.

The final HIL rerun closed the ambiguity. With an unfiltered data socket, an
all-error-frame socket, and all eight observer filters proven active, one
authorized read-only SDO upload produced the exact 0x601 request and 0x581
response in the target raw capture. The normal observer published snapshot
version 2 and preserved the 0x581 response as the expected `sdo_rejected`
observation. RXF/RXMF advanced by two, target TX remained zero, and no error
frame was observed.

No CAN interface configuration, JCAN configuration write, SDO download, NMT,
RPDO, CiA402 command, drive parameter change, service installation, or motion
operation was performed by the agent. Agent-performed physical CAN traffic was
limited to five authorized identical SDO upload requests: four
response-validating reads and one time-aligned send-only diagnostic. The
operator separately ran the documented periodic 0x7FF receive control.

## Local qualification

| Gate | Result | Evidence |
| --- | --- | --- |
| Dependency pair | PASS | CANopenLinux f1348d4 and CANopenNode ef9ac3a; clean nested gitlink regression |
| Host Debug | PASS | 28/28, including managed CANopen and SocketCAN vcan tests |
| Host Release | PASS | 28/28, including managed CANopen and SocketCAN vcan tests |
| Commissioning Debug | PASS | 36/36, including commissioning gate and managed-vcan regression |
| ASan/UBSan | PASS | 36/36 with ASAN_OPTIONS=detect_leaks=0 |
| LLVM | PASS | clang-format and scoped clang-tidy 22.1.8 over the observer sources |
| Whitespace | PASS | git diff --check |

The first restricted Host Debug CTest attempt passed 25 tests, skipped the two
managed-vcan tests, and failed the SocketCAN missing-interface errno contract
because the sandbox denied networking. The isolated SocketCAN test passed
outside the sandbox, both managed-vcan programs then passed, and one complete
unrestricted Host Debug attempt passed 28/28 with no skip. The initial result
is retained as an infrastructure classification rather than hidden by retry.

The managed CANopen run preserved boot, heartbeat, EMCY, TPDO1 through TPDO4,
CAN error frames, timeout and reopen behavior, and reported
observer_tx_frames=0. The managed SocketCAN run preserved filter isolation,
RX overflow, CAN error frames, and link down/reopen behavior.

Local logs were retained under /tmp/robot_control_p57_*.log for this run.

## RK3588 cross evidence

Both network-disabled builds used GCC 11.4, the a685ab13 Ubuntu 22.04 sysroot,
and a clean deterministic source snapshot:

- revision: 0d32ca928e6c3969bd8a1007870a10e1b7e5cb27
- dirty: false
- source files: 676
- snapshot SHA-256: e7247951e8ee205f0bc48dea960aa528de62a023c9b5a7045db860dfaebd6b8a

| Preset | Observer SHA-256 | Metadata SHA-256 | Result |
| --- | --- | --- | --- |
| rk3588-debug | 288bd769710bed7fc88f32957d7313a06a93e0784806db12286ff3b1d056ac4b | 44a832d966ed19ff7064a9053648f022673696d368aeede4b1c23efad676d06d | PASS |
| rk3588-release | c4325501f31625c72b48c3bd772e61cb1036941ecc11f13b090c172eae0baf33 | 69919538853cd93b4c07c2c4a1cc467d02debb017f6bde419e64683b107c1a4f | PASS |

Both observer artifacts are AArch64 PIE executables using
/lib/ld-linux-aarch64.so.1, contain no RPATH/RUNPATH, and require only libraries
present in the locked sysroot. Their symbol tables contain CO_CANsend and
robot_control_canopen_deny_transmit and do not contain the commissioning
transmit gate.

## Semantic review

CodeGraph traced the observer through Lifecycle creation, the monotonic bounded
run loop, immutable observation snapshots, synchronous termination, and the
normal upstream transmit boundary. The normal boundary resolves send attempts
to robot_control_canopen_deny_transmit, which returns EACCES before a socket
syscall. The commissioning session and commissioning transmit authorization are
not linked into the normal observer artifact.

Scoped clang-tidy reported no analyzer, bugprone, performance, or portability
finding. No blocking correctness, safety, concurrency, portability, security,
or test-adequacy finding remains for the P5.7 observer change.

## JCAN passive evidence

The selected JTool-CAN serial is 207F346D5650. Scan, raw configuration read,
profile read, periodic-task inspection, and every capture returned ok=true with
no warning. A bounded three-frame internal silent-loopback also passed without
placing traffic on the physical bus. The following configuration read matched
the initial baseline, and active and recent periodic task lists were empty. No
physical-bus frame or configuration write was performed.

Relevant MCP evidence:

- scan: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T013306.773966Z-1788485586773994001-scan.json
- configuration: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T013329.716872Z-1788485609716892111-get_config.json
- can0-UP baseline capture: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T014941.055602Z-1788486581055630184-capture.json
- deadline-window capture: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T015032.375763Z-1788486632375799733-capture.json
- SIGTERM-window capture: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T015127.396918Z-1788486687396947528-capture.json
- final periodic cleanup: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T015201.895840Z-1788486721895869387-periodic_list.json
- internal silent-loopback: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T015655.980012Z-1788487015980036392-loopback_test.json
- post-loopback configuration: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T015702.421815Z-1788487022421839092-get_config.json
- post-loopback periodic cleanup: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T015707.514839Z-1788487027514866470-periodic_list.json

Each passive capture ran for approximately 5000 ms with a 1000-frame bound and
reported received_frames=0, matched_frames=0, dropped_frames=0. The successful
internal loopback establishes that the analyzer USB/protocol path is operating.
Together these results prove no observer frame reached the independent analyzer
during either passive execution window.

### Authorized active SDO subgate

The user confirmed the drive was powered and explicitly authorized bounded JCAN
transmission. The preflight at /tmp/robot_control_p57_sdo_preflight.yaml limited
the operation to one standard Classical CAN request: ID 0x601, DLC 8, payload
40 08 20 00 00 00 00 00. It prohibited downloads, NMT, RPDO, periodic traffic,
configuration writes, resets, and any other identifier or payload.

JCAN returned ok=true with no warning and recorded:

- request: 40 08 20 00 00 00 00 00
- response: 4B 08 20 00 C8 00 00 00
- value: C8 00, unsigned 200
- evidence: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T020929.834522Z-1788487769834558358-sdo_read.json

Postflight showed can0 RX=2, TX=0, zero bus errors, no observer process or CAN
receiver, unchanged JCAN configuration, and no active or recent periodic task.
The normal observer run overlapped the transaction but published only its
initial snapshot. CodeGraph and Serena confirmed that 0x581 is installed in the
normal filter and ObservationStore would record this frame as sdo_rejected.

### Final authorized retry

The user explicitly authorized one final retry. The preflight at
/tmp/robot_control_p57_sdo_retry_preflight.yaml prohibited every other frame and
any further retry. Before transmission, the remote harness reported
readiness=pass and listed all eight exact normal filters on can0, including
0x581. The observer PID was 4798.

JCAN again returned ok=true with no warning:

- request: 40 08 20 00 00 00 00 00
- response: 4B 08 20 00 C8 00 00 00
- value: C8 00, unsigned 200
- evidence: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T021938.091374Z-1788488378091390681-sdo_read.json

The observer completed its 15000 ms deadline with snapshot version 1 and no
sdo_rejected observation. Target postflight recorded RXF=4, RXMF=0, can0 RX=4,
TX=0, zero errors, no receiver, and no process. JCAN configuration remained
unchanged and periodic cleanup was empty:

- periodic cleanup: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T022028.912790Z-1788488428912818700-periodic_list.json
- configuration: /home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T022035.886976Z-1788488435887008232-get_config.json

The final retry therefore confirms drive response and observer zero TX but does
not satisfy raw target observation. No third transmission was attempted in that
session.

### Resumed unfiltered target diagnostic

After SSH connectivity was restored, the user authorized continuation of the
interrupted diagnostic. JCAN scan selected explicit serial 207F346D5650;
configuration readback matched the earlier baseline, the 500 kbit/s physical
profile returned ok=true without warning, and active/recent periodic lists were
empty. A 3-second passive capture received zero frames.

The safety preflights and complete evidence bundle are stored under
`docs/verification/evidence/p5_7_20260904/`. A first 30-second setup window
expired before stimulus. A requested 90000 ms observer duration was rejected by
argument validation; its raw-capture process was stopped and no frame was sent.

The final 60000 ms window started an unfiltered `candump` and the normal
observer. Before stimulus, the harness proved both processes alive and recorded
the observer's eight exact `rx_sff` filters: 0x000, 0x081, 0x181, 0x281, 0x381,
0x481, 0x581, and 0x701. JCAN then performed exactly one authorized expedited
upload:

- request: 40 08 20 00 00 00 00 00
- response: 4B 08 20 00 C8 00 00 00
- value: C8 00, unsigned 200
- evidence: docs/verification/evidence/p5_7_20260904/20260904T024928.369831Z-1788490168369855786-sdo_read.json

Target file times bound the receive window to approximately 02:48:00.509
through 02:49:01.516 UTC. The JCAN success record is timestamped 02:49:28.369
UTC and contains no physical transmit timestamp. The observer remained at
version 1 and the unfiltered raw log remained empty. Postflight showed can0
RX=6, TX=0, RXF=6, RXMF=0, zero errors, no receiver, and no observer process.
JCAN configuration remained unchanged and periodic cleanup was empty. The run
therefore preserves the active response and cleanup evidence but cannot close
the positive target-RX gate or establish a driver defect.

### Time-aligned send-once diagnostic

The user explicitly authorized one additional immediate frame with no retry:
standard ID 0x601, DLC 8, payload 40 08 20 00 00 00 00 00. New storage and
bus-write preflights are preserved in the evidence bundle. Target baseline was
can0 RX=6, TX=0, ERROR-ACTIVE with zero errors; JCAN configuration was unchanged
and periodic lists were empty.

The final receive window ran from 03:12:48.384 through 03:13:49.387 UTC. At
03:12:49.390 UTC the harness proved an unfiltered raw socket and all eight exact
observer filters active. `jcan_send_once` returned ok=true without warning and
wrote its success evidence at 03:13:08.411 UTC, inside the target window. Live
inspection at 03:13:36.798 UTC showed can0 RX=7 and both sockets still
registered, but the unfiltered socket and every exact filter had zero matches.
The raw log was empty and the observer exited normally at snapshot version 1.

Postflight recorded RX=7, TX=0, RXF=7, RXMF=0, zero bus errors, no receiver, and
no process. JCAN configuration remained unchanged and periodic cleanup was
empty. The target identifies the driver as `rockchip_canfd` version 6.1.84 on
platform device `fea60000.can`. This time-aligned result rules out the observer
filter/parser as the immediate cause for that one-shot experiment, but it does
not prove that a valid 0x601 data frame reached the wire or that the drive
responded during the send-only operation. Full evidence is in
`docs/verification/evidence/p5_7_20260904/send_once_diagnostic_result.md`.

### Operator periodic 0x7FF control

The operator then ran `candump -ta can0` while JCAN transmitted standard ID
0x7FF, DLC 8, payload 01 02 03 04 05 06 07 08 every 50 ms. The supplied trace
shows continuous correctly decoded frames at the expected interval. Read-only
postflight recorded RXF=1207, RXMF=393, a 20 frames/s maximum receive rate,
TX=0, and zero CAN errors.

This is a PASS for the general can0/SocketCAN/candump receive path and supersedes
the generic `rockchip_canfd` delivery diagnosis. It does not close P5.7 because
0x7FF is not a drive-originated CANopen frame accepted by the normal observer.
The unresolved scope is now JCAN single-shot physical behavior, ID 0x601, or a
possible unmatched CAN error frame. Evidence is in
`docs/verification/evidence/p5_7_20260904/periodic_7ff_control.md`.

### Final error-enabled HIL rerun

The user then requested a HIL rerun. The first receiver setup rejected the
incompatible `candump -L -e` option combination and exited before stimulus; the
safety gate stopped the observer and no CAN frame was sent. The retained retry
used `candump -ta -e can0` and proved an unfiltered data socket, an all-error
socket, and all eight observer filters active before the one authorized SDO
upload.

The target raw log preserved both frames:

    (1788494843.268321) can0 601 [8] 40 08 20 00 00 00 00 00
    (1788494843.268631) can0 581 [8] 4B 08 20 00 C8 00 00 00

The observer then published snapshot version 2 with one `sdo_rejected`
observation containing raw ID 0x581, DLC 8, and the exact response payload.
This rejection is expected in normal observer mode because it owns no active
commissioning request context. RXF/RXMF advanced from 1207/393 to 1209/395,
target TX remained zero, no error frame was captured, and all cleanup checks
passed. See
`docs/verification/evidence/p5_7_20260904/hil_sdo_rerun_result.md`.

## Target deployment and runtime

The target was reached through SSH alias robot-dev as user cat, UID/GID 1000.
It identified as lubancat, Ubuntu 22.04.5 LTS, kernel 6.1.84, aarch64. The
network address is intentionally omitted from versioned evidence.

The storage-write preflight is
/tmp/robot_control_p57_target_preflight.yaml. It authorized one non-overwriting
SCP operation to /tmp/robot-control-canopen-observer-0d32ca9 and prohibited
privilege escalation, CAN configuration, service changes, and execution during
the copy operation.

The staged file is owned by cat:cat, mode 0755, size 1089416 bytes. Its target
SHA-256 exactly matches the reviewed Debug artifact. Target readelf, ldd, and
--help checks passed.

The operator configured can0 outside the agent command set. Fresh read-only
inventory confirmed UP, LOWER_UP, ERROR-ACTIVE, bitrate 500000, sample point
0.866, restart-ms 100, zero bus-error counters, and no pre-existing CAN
receiver.

### Deadline run

The normal observer used controller node 127, remote node 1, heartbeat timeout
1500 ms, SDO timeout 500 ms, TPDO timeout 300 ms, expected DLC 8,0,0,0, and a
3000 ms duration. It exited 0:

    event=start observer_version=1 interface=can0 controller=127 remote=1 duration_ms=3000
    event=snapshot version=1 transport=1 boot=0 malformed_count=0 replay_count=0 sdo_rejection_count=0
    event=summary reason=deadline version=1

The independent JCAN capture covering the run received zero frames. Target
postflight showed can0 RX=0, TX=0, no errors, no receiver, and no observer
process.

### SIGTERM run

The same normal observer configuration used a 60000 ms duration. The harness
recorded the observer PID, waited one second, sent SIGTERM only to that PID,
and waited for its exit:

    event=start observer_version=1 interface=can0 controller=127 remote=1 duration_ms=60000
    event=snapshot version=1 transport=1 boot=0 malformed_count=0 replay_count=0 sdo_rejection_count=0
    event=summary reason=signal signal=15 version=1
    event=harness observer_pid=2946 kill_exit=0 wait_exit=143

The independent JCAN capture again received zero frames. Final target postflight
confirmed can0 remained UP/ERROR-ACTIVE with RX=0, TX=0 and zero error counters;
no CAN receiver or observer process remained. The staged executable remains in
/tmp because cleanup was not requested.

## Acceptance matrix

| P5.7 acceptance item | Result | Evidence |
| --- | --- | --- |
| Host and managed-vcan tests pass without skip | PASS | Debug 28/28; Release 28/28; commissioning 36/36 |
| Sanitizer and changed-source static checks | PASS | ASan/UBSan 36/36; LLVM 22.1.8 pass |
| RK3588 Debug and Release match reviewed source/sysroot | PASS | clean metadata, hashes, and ELF audit |
| Ordinary-user target observer opens can0 | PASS | start record as UID/GID 1000 |
| Deadline and SIGTERM are bounded | PASS | exits 0 and 143 with exact summaries |
| Observer leaves no process or CAN receiver | PASS | target postflight |
| Independent capture proves normal observer emitted no frame | PASS | two synchronized JCAN captures, zero frames; target TX remained zero |
| Drive responds to bounded read-only SDO upload | PASS | four exact 0x601 requests received the same 0x581 response; final raw capture preserved request and response |
| Periodic target raw delivery control | PASS | 0x7FF at 50 ms reached candump; RXF=1207, RXMF=393, maximum 20 frames/s |
| Earlier time-aligned 0x601 send-once diagnostic | FAIL (historical) | can0 RXF advanced from six to seven without a socket match; retained as audit evidence |
| Observer preserves target drive CANopen evidence | PASS | final HIL snapshot version 2 preserved raw 0x581/DLC8/payload as expected sdo_rejected |
| Final Phase 5 closure | PASS | raw request/response, observer evidence, RXMF +2, zero target TX, zero errors, and cleanup all pass |

## Evidence contracts

    deployment_result:
      schema_version: 1
      created_at_utc: "2026-09-04T01:46:02Z"
      safety_preflight: "/tmp/robot_control_p57_target_preflight.yaml"
      target: {board: "RK3588 lubancat", hardware_revision: "unknown"}
      tool: {id: "scp", version: "system OpenSSH"}
      artifact: "out/build/cross/rk3588-debug/tools/canopen_observer/robot-control-canopen-observer"
      artifact_sha256: "288bd769710bed7fc88f32957d7313a06a93e0784806db12286ff3b1d056ac4b"
      destination: "/tmp/robot-control-canopen-observer-0d32ca9"
      verified: true
      reset_after_deploy: false
      previous_state: "destination absent"
      final_state: "staged executable present; no service installed"
      rollback: {required: false, exercised: false, result: "not-applicable"}

    bus_validation:
      schema_version: 1
      created_at_utc: "2026-09-04T01:51:32Z"
      interface: "can"
      adapter_or_resource: "JTool-CAN 207F346D5650"
      settings:
        can: {nominal_bitrate: "500000", listen_only: true}
      requirements: ["P5.7-zero-normal-TX"]
      measurements_or_responses: "three captures: zero received, matched, and dropped frames"
      raw_capture: "/home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T015032.375763Z-1788486632375799733-capture.json and 20260904T015127.396918Z-1788486687396947528-capture.json"
      report: "docs/verification/PHASE5_CANOPEN_BASELINE.md"
      cleanup: {required: false, completed: true, final_state: "no periodic task active"}
      passed: true

    bus_validation_active_sdo:
      schema_version: 1
      created_at_utc: "2026-09-04T02:09:29Z"
      safety_preflight: "/tmp/robot_control_p57_sdo_preflight.yaml"
      interface: "can"
      adapter_or_resource: "JTool-CAN 207F346D5650"
      settings:
        can: {nominal_bitrate: "500000", listen_only: false}
      requirements: ["P5.7-drive-response"]
      stimulus: "one 0x601 DLC8 frame: 40 08 20 00 00 00 00 00"
      measurements_or_responses: "0x581 DLC8: 4B 08 20 00 C8 00 00 00"
      raw_capture: "/home/gtc/Desktop/workspace/JCAN/evidence/runtime/20260904T020929.834522Z-1788487769834558358-sdo_read.json"
      report: "docs/verification/PHASE5_CANOPEN_BASELINE.md"
      cleanup: {required: true, completed: true, final_state: "no periodic task; configuration unchanged"}
      passed: true

    test_result_active_observer_capture:
      schema_version: 1
      source_revision: "0d32ca928e6c3969bd8a1007870a10e1b7e5cb27"
      safety_preflight: "/tmp/robot_control_p57_sdo_preflight.yaml"
      level: "hil"
      target: "RK3588 lubancat and node-1 CANopen drive"
      attempts: 4
      passed: false
      counts: {total: 1, passed: 0, failed: 1, skipped: 0}
      classified_failures: ["first-attempt-observer-readiness-not-proven", "second-attempt-rxf-four-rxmf-zero-no-observation", "third-attempt-jcan-record-after-target-window-no-transmit-timestamp", "fourth-attempt-time-aligned-rxf-seven-rxmf-zero"]
      requirements: ["P5.7-positive-target-RX"]

    test_result_final_hil:
      schema_version: 1
      source_revision: "0d32ca928e6c3969bd8a1007870a10e1b7e5cb27"
      safety_preflight: "docs/verification/evidence/p5_7_20260904/hil_sdo_rerun_preflight.yaml"
      level: "hil"
      target: "RK3588 lubancat and node-1 CANopen drive"
      attempts: 2
      passed: true
      counts: {total: 1, passed: 1, failed: 0, skipped: 0}
      classified_failures: ["first-receiver-setup-invalid-candump-option-combination-no-stimulus"]
      log: "docs/verification/evidence/p5_7_20260904/hil_sdo_rerun_result.md"
      requirements: ["P5.7-positive-target-RX", "P5.7-zero-normal-TX"]

    release_evidence:
      schema_version: 1
      version: "P5.7-complete"
      source_revision: "0d32ca928e6c3969bd8a1007870a10e1b7e5cb27"
      safety_preflights: ["/tmp/robot_control_p57_target_preflight.yaml", "/tmp/robot_control_p57_sdo_preflight.yaml", "/tmp/robot_control_p57_sdo_retry_preflight.yaml", "docs/verification/evidence/p5_7_20260904/target_capture_preflight.yaml", "docs/verification/evidence/p5_7_20260904/target_capture_retry_preflight.yaml", "docs/verification/evidence/p5_7_20260904/target_capture_final_preflight.yaml", "docs/verification/evidence/p5_7_20260904/can_write_preflight.yaml", "docs/verification/evidence/p5_7_20260904/target_capture_send_once_preflight.yaml", "docs/verification/evidence/p5_7_20260904/can_send_once_preflight.yaml", "docs/verification/evidence/p5_7_20260904/hil_sdo_rerun_preflight.yaml", "docs/verification/evidence/p5_7_20260904/hil_sdo_rerun_capture_preflight.yaml", "docs/verification/evidence/p5_7_20260904/hil_sdo_rerun_capture_retry_preflight.yaml"]
      build_outputs: ["host-debug", "host-release", "commissioning-debug", "sanitize", "rk3588-debug", "rk3588-release"]
      deployments: ["RK3588 /tmp observer staging"]
      debug_sessions: []
      runtime_monitors: ["target deadline stdout", "target SIGTERM stdout", "p5_7_20260904 observer/raw/error target logs"]
      bus_validations: ["managed-vcan", "JCAN passive zero-TX pass", "four JCAN active SDO responses pass", "historical failed/timing-inconclusive captures", "operator 50 ms 0x7FF raw receive control pass", "final target raw plus observer HIL pass"]
      tests: ["28/28 Debug", "28/28 Release", "36/36 commissioning", "36/36 ASan-UBSan", "docs/verification/evidence/p5_7_20260904/hil_sdo_rerun_test_result.yaml", "docs/verification/evidence/p5_7_20260904/local_ci_result.md"]
      reviews: ["CodeGraph transmit-path review", "LLVM 22.1.8"]

## Closure

Phase 5 is complete. The final error-enabled HIL window preserved the exact
0x601 request and 0x581 drive response, the normal observer published the raw
response, RXF/RXMF each advanced by two, target TX stayed zero, and cleanup
passed. Earlier failed and timing-inconclusive attempts remain in the evidence
bundle and were not silently converted into passes.

Residual hardware claims remain outside Phase 5: no CiA402 motion, NMT
transition, SDO download, persistent configuration, drive fault/brake behavior,
electrical bus-off, or load/soak acceptance was exercised. No further physical
transmission, interface reset, network configuration, kernel deployment, NMT,
SDO download, drive reset, power cycle, or periodic transmission is authorized.
