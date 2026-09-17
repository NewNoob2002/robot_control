# F4 A4 — INCOMPLETE: invalid startup SBUS wrongly rejected as reverse input

User requested repair/retest, then explicitly permitted a larger motion window
because transmitter power needs a1–2s long press and has no screen/progress cue.
New82318b05 uses --motion-window-ms8000; CLI default remains3000. Both proactive
cutoff (7950ms) and independent send gate (8000ms) share the configured duration.
Targets remain right-only0..5rpm, left0, feedback±1.5rpm, no auto-rearm/retry.

New POWER_OFF_READY is emitted by the target runner after positive right feedback
has been observed for1s. It only prompts; it cannot create/override motion commands.
MOTION_READY asks for forward throttle with neutral steering. POWER_OFF_READY asks
the operator to long-press transmitter power until OFF, keeping throttle until OFF,
then neutral. Receiver continuously powered; X1 reset initially/available for any
unexpected behavior. Transmitter stays OFF until drive OFF. Local START and fresh
physical-readiness confirmation precede independent JCAN capture/target candump.

Preparation: Debug44/44 and ASan/UBSan44/44, no skips; default and8s gate boundaries,
invalid CLI and4s delayed failsafe/8s cutoff vcan scenarios pass. Scoped static,
locked-image/real-sysroot cross and qualification ELF audit pass.109 C/C++ inputs
match the immutable build snapshot. See ../motion_window_8s_preparation.
Runner checks cover live/expired prompts, no start without readiness, feedback cue
at1s only, explicit8s causal analysis and preserved default3s rejection.

Acceptance still requires actual raw SBUS loss before first zero and7950ms cutoff,
Source withdrawal, <=100ms receive/deadline-to-zero, no subsequent nonzero,
>=150ms stable band within1s, matching dual capture and full volatile restoration.
Actual RF switch latency is not directly measured; valid-frame timeout is not
claimed as electrical UART silence. A1/A2/A3 failures remain unchanged.

## Actual result

A4 consumed both markers but never enabled/moved. Transmitter was not switched ON
(operator clarification). Receiver flags12/CH3=0 mapped to a negative raw candidate;
the right-throttle guard incorrectly evaluated it despite invalid/disabled Source.
Application control_loop_right_throttle failed before MOTION_READY (cycles1,
enabled_samples0,1.576s invocation).350 matching capture frames, all RPDO targets
zero,36 exact volatile writes/full baseline restoration,357 trace rows. The original
terminal OFF fields are empty; user later explicitly confirmed “已关闭” in chat.
Actual local statement “什么都没干就退出来” is retained; no sound/direction
confirmation is fabricated. This is a startup-wait defect, not an8s-window result.
A5 must use a new fixed artifact and fresh readiness; A4 remains INCOMPLETE.
