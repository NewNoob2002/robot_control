# P10.3 initial physical prerequisites

See [checkpoint](../../P10_3_HIL_CHECKPOINT.md). Full P10.3 remains OPEN.

Initial binaries came from the P10.2 verified snapshot. TPDO2 qualification was
cross-built from a dirty worktree based on60eb391. Cross metadata and source
checksums identify the source/image/sysroot/ELF. Replaying cross-diagnostics.py
requires a fresh snapshot destination; do not overwrite retained snapshots.

Initial files record no-device smoke and a receive-only capture; raw UART is
sbus-readonly.log.gz. diagnostics-target holds application/candump/counters;
diagnostics-jcan-attempt2 holds independent JSONL and clean CANStop response.
Run analyze-diagnostics.py to compare all252 frames and exact writes/restoration.
Operator confirmation is separate. Checksummed compiled code matches the cross
snapshot; documentation/evidence were completed afterward.

diagnostics-jcan-once is the FAILED initial preparation; target did NOT start.
jcan-window-attempt1.py is that original consumed runner. Attempt2 changed
orchestration, not the firmware or hardware oracle. The first driver trial
succeeded and its physical-once marker is consumed. All runners and readiness
records are historical evidence, not permission for another test.

Compressed logs retain Debug/P6 and sanitizer78/78, focused4/4, pre-method
compile failure, failed restore oracle, static corrections/final pass, cross/ELF
and CI routing checks. The final narrow run follows the endl/stop-token fixes.
No hardware movement, network configuration or persistent drive change occurred.
Final operator-confirmed drive power OFF; can0 left as found. Full-chain HIL is
not accepted by this prerequisite.
