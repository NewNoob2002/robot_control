# Synchronous stop/loss requalification preparation

Artifact fa1944fdba2d preserves synchronous mode for all owned first-motion and
stop/loss sequences. It waits through bounded deceleration and checks watchdog
stopping with passive TPDOs. The first post-quiet TX clears the packed target;
there is no preliminary SDO read capable of refreshing a retained target.

Host 60/60 and ASan/UBSan 60/60 pass. Clean pinned GCC11.4 aarch64 build, ELF
dependency/interpreter/version audit and target isolated-vcan pass; the target
test reports prohibited=0, failures=0. Default Debug/Release builds succeed
without changes. Static checks complete with no errors and eight existing
advisories. Manifest records staged file and source hashes.

Watchdog, heartbeat-loss, TPDO-loss, Shutdown, Disable Voltage, Quick Stop and
NMT Stop each have a distinct prepared one-shot runner and recorded bounds.
Initially two watchdog execution requests timed out in automatic approval
before process creation. A resumed request was rejected before process creation;
both records remain preserved. The operator then explicitly authorized the exact
watchdog action and current readiness with “授权并开始”.

The watchdog runner subsequently executed once and exited 0. Its analyzer exits
0: 168 frames match exactly between JCAN and target candump, 55 SDO transactions
complete, and CAN error/drop counters remain zero. The TX-free interval is
1503.019 ms; first zero-speed TPDO arrives 634.003 ms after the last request.
The first resumed TX clears both targets. Left TPDO speed stays zero; right
speed is nonzero before stopping. Zero feedback, Shutdown/Pre-operational,
temporary timer restoration and capture/executor cleanup pass. The operator
confirms right-wheel motion then stopping, left stationary, no abnormal sound
and no restart. This bounded watchdog trial passes; final phase acceptance
remains open. See watchdog/analysis.json and watchdog/target/ for raw evidence.

The separately authorized heartbeat-loss runner subsequently executed once and
exited 0. Its analyzer exits 0: 195 identical dual-capture frames, 76 SDO
transactions, target-to-zero request 601.994 ms, zero CAN errors/drops, verified
zero feedback, parameter restoration and cleanup. The operator confirms right-wheel
motion then stopping, left stationary, no abnormal sound and no restart: PASS.
See heartbeat_loss/analysis.json and heartbeat_loss/target/.

The separately authorized TPDO-loss runner executed once and exited 0. Its
analyzer exits 0: 171 identical dual-capture frames, 73 SDO transactions,
target-to-zero request 306.001 ms, zero CAN errors/drops, zero-speed feedback
and restoration of the 100 ms TPDO1 timer. The operator confirms right-wheel
motion then stopping, left stationary, no abnormal sound and no restart: PASS.
See tpdo_loss/analysis.json and tpdo_loss/target/.

Shutdown and Disable Voltage each executed once with runner/analyzer exit 0,
164 matching capture frames, 59 SDO transactions and zero errors/drops.
Shutdown and Disable Voltage operator confirmations are accepted.
Their stop commands arrive 1001.063/1000.999 ms after the nonzero target.
Both have a first zero TPDO followed by short signed speed excursions; trailing
observed zero begins 240.099/240.688 ms after the stop command. Preserve the raw
feedback: first zero is not sustained stopping. The application waits for further
independent zero feedback before cleanup. See each stop_timing.json and the
reproducible analyze_stop_timing.py. Sampled timing is not a hard-real-time bound.

Quick Stop executed once with runner/analyzer exit 0: 151 matching frames,
52 SDO transactions, zero errors/drops and verified Quick Stop Active terminal
state. The stop command arrives 1001.046 ms after the nonzero target, and zero
TPDO 289.540 ms after that command. Signed speed excursions during stopping
remain in the capture. Operator confirmation is accepted: PASS.

The NMT Stop runner executed once but failed during startup, before any nonzero
target or NMT Stop command. All 172 frames match across captures and kernel
packet/byte counters; error/drop counters are zero. Every TPDO velocity is zero
and dual status remains raw 07140714 (Quick Stop Active), inherited from the
previous test. Both startup and cleanup Shutdown commands are acknowledged but
the expected state never appears. Application and capture processes stop;
drive-state cleanup is NOT verified. The remote wrapper then reports the
application diagnostic as an assertion failure; that wrapper traceback is
secondary to the state timeout. See nmt_stop/failure_analysis.json and raw logs.

The source path enter_zero_target_operation_enabled unconditionally begins with
Shutdown, while cleanup_zero_target_cia402 also expects Ready to Switch On after
Shutdown unless a terminal controlword was already submitted. The completed
Quick Stop trial intentionally retains Quick Stop Active. This cross-trial
startup-state incompatibility must be resolved and qualified; no automatic
enable, recovery command, power cycle or physical retry was attempted.

Six bounded tests pass with operator confirmation. NMT Stop remains unqualified;
its one-shot startup failed. Further physical tests are stopped. Moving SIGTERM,
controlled link/cable/power loss, applicability/fault/soak decisions and final
regression remain open. This is not Phase 6 closure.
