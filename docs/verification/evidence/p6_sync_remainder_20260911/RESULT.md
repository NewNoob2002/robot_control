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
None ran: two watchdog execution requests timed out in automatic approval
before process creation, including one permitted request retry. See
execution_blocker.json. No marker or coordinator result exists for any trial.
This is an execution-review blocker; no physical test failure is implied.

The application is safely idle after the earlier successful RPDO right trial.
No drive mode, mapping, watchdog, or target changes were performed by these
blocked requests. The next action is renewed execution review for the same
watchdog runner; do not repeat the successful synchronous or RPDO trials.
