# Explicit motion window for long-press transmitter shutdown

User requested repair/retest and allowed a larger window. Debug-only/default-OFF
HIL accepts --motion-window-ms3000..8000 only in motion modes; default3000.
Both MotionGate::done and final send rejection use the injected duration (50ms
proactive margin). Invalid constructor values permanently inhibit nonzero; zero,
first-zero latch, monotonic guard, single-wheel5rpm bound and fault withdrawal stay.
Control-start/motion-ready logs report configured duration; independent feedback
analysis defaults to3000 and needs an explicit expected value for longer trials.
No production domain, receiver timeout, safety policy or rearm change.

Unit tests added before implementation first failed to compile for the missing
window constructor. New injected-clock tests cover default/8s endpoints, invalid
windows and permanent no-restart after stop. Real vcan/PTY executable tests inject
failsafe after4s and check <100ms stop; a separate8s cutoff case checks timeout.
Both reject later nonzero. Malformed, duplicate and non-motion CLI options reject.
Debug44/44 and ASan/UBSan44/44 no skips; scoped clang-tidy emits no project diagnostics.
Locked Docker image and target Ubuntu22.04 sysroot cross build and qualification
ELF audit pass;109 C/C++ files match the build snapshot. See retained logs/metadata.
The initial generic Phase3 ELF audit was inapplicable (clock_nanosleep probe symbol);
established qualification ELF audit is the applicable check and passed.

A4 is a fresh one-shot using82318b05 and feedback-driven local power-off cue after1s.
The user-authorized physical trial and final acceptance are recorded separately.
