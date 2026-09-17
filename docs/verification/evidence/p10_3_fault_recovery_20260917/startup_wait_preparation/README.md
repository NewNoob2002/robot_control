# Startup input wait and operator reminders

A4 failed before motion: receiver emitted flags12/CH3=0 while transmitter was OFF.
Invalid/disabled raw candidates were checked as authorized right-throttle commands.
ControlLoop now applies that qualification restriction only to valid enabled
samples; Source/raw candidates and the arbiter/safety invalid-input zero policy
remain unchanged. The HIL candidate envelope permits unready input only before
first enable, while still enforcing CAN health/feedback standstill bounds.
After any enabled observation, authority loss still terminates the one-shot;
no waiting loop or automatic rearm is added after motion.

The original control duration (60s in A5) bounds waiting plus operation, never
restarts on recovery. New event=input_status values waiting_link/neutral_required/
release_ch6/ready/arming drive local operator prompts. Disabled healthy neutral
input with CH6 released is required before the ready prompt; a fresh CH6 edge is
still required by Source. UART transport errors and CAN errors still stop, not wait.
If the receiver/transmitter never becomes ready, timeout cleans up and fails.
A5 startup may have transmitter OFF; receiver stays powered.8s motion window and
feedback-driven POWER_OFF_READY remain as separately approved.

Regression was added first and reproduced A4's error. Isolated vcan/PTY tests cover
flags12, silent input, nonneutral/held button, fresh release/press, and bounded
never-ready timeout. Existing active loss/failsafe tests must continue to pass.
Debug45/45 and ASan/UBSan45/45 passed with no skips. Scoped static, locked cross and
qualification ELF checks passed;109 C/C++ inputs match the immutable snapshot.
A5 audit isolates motion-phase loss from earlier startup flags, rejecting reuse of
startup loss as the moving stimulus. Previous attempts are preserved.
