# Manual TPDO configuration/capture preparation

Adds fixed --manual-tpdo to the Debug-only qualification artifact. The supplied
STM32 mapping is written and read back in Pre-operational, then a60s Operational
window captures hand rotation. All motor target/controlword writes are excluded
from this operation and its independent frame guard. Both axes must be in a
reviewed non-enabled state. Original mapping/heartbeat are restored on success
and attempted after partial-write failure or interruption; restoration failure
is reported without hiding the original failure.

Tests were added before implementation. The first gate build failed because the
new authorization function did not yet exist. Focused tests then passed; full
host Debug and LLVM ASan/UBSan pass53/53 each. Fifteen managed-vcan manual cases
cover nominal capture, each of eight applied-write/lost-ACK positions, enabled
startup, baseline mismatch, missing TPDO, SIGTERM, enabled remapped feedback and
restoration failure. Default Debug/Release pass28/28 each; P5.6 passes36/36.
Pinned RK3588 Debug build and ELF/sysroot audit pass. Static analysis reports no
errors and21 reviewed advisories: existing copy/optional-access/parameter findings,
one bounded fixed-object lambda parameter warning, and intentional readiness/end
flushes. See test_result.json and preserved logs.

Frame guard allows only the fixed mapping/heartbeat downloads, fixed uploads and
NMT commands. Maximum18 downloads,300 uploads,3 NMT,10000 captured frames and95s
capture. Physical attempt1 subsequently ran; see ../p6_6_20260910_manual_tpdo_trial_1/RESULT.md.
