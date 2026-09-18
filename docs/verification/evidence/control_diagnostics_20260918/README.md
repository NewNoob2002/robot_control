# Continuous diagnostics / zero-soak software preparation — 2026-09-18

G1 implements bounded nonblocking trace delivery to a separate EasyLogger
consumer, one-second diagnostic snapshots, a one-hour-only zero-target mode,
and target supervision with resource/CAN/lease checks. Existing short-HIL and
motion limits remain unchanged. See [the interface contract](../../../development/CONTINUOUS_DIAGNOSTICS.md).

Debug and ASan/UBSan each pass48/48, no skips. Scoped clang-tidy, locked-image/
real-sysroot aarch64 builds and ELF audits pass. The streaming vcan/PTY fixture
also injects collector death/suspension and verifies bounded stop/restoration.
The first collector test failed under the host sandbox because the Linux logger
port could not access its output path; the same test passes outside the sandbox.
No production logger workaround or masking was added.

Artifacts: control-hil0ce0bb145f403cc32d3d99840cc7553181b2f7bc98b00244e44eb2d482d6daa9;
diagnostics e968b9d7332332a0e4068b4017095e18e3ff1c809372b0e40abaadc1b72f5d0e.
Hardware trials and acceptance belong to their own fresh records, not this
software result. User confirms raised wheels, online SBUS, neutral controls,
CH6 released and emergency-stop access; software preparation keeps drive OFF.

Final comment/format cleanup was rebuilt and the focused three-case diagnostic/
trace/stream suite passes again. The final supervisor checks pass with inert
processes; final static and ELF checks pass. New v2 source snapshot/artifact
identity supersedes v1 for deployment (v1 --help target smoke was device-free).
Final control-hil: bec74e79c8475fcd0b0c9a2b1696e0d76815987f145c16d8fc8647e17b859e6f.
Final diagnostics: 9a226adb3e1d7b2380970054661a86739b9c10c2a9eba5716e234dcf5c13252b.
The target /opt staging location is not writable by the development account;
use the separate user-owned .cache/robot-control/staging directory, without
privilege or production changes. No motion/soak is claimed by these smoke checks.
