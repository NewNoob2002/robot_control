# Stop feedback: deferred review (2026-09-17)

User requested a new F4 trial and recording feedback errors for end-of-test review.
After a successful zero RPDO following motion, selected-wheel negative feedback
outside the configured near-zero band but within−7.5rpm is recorded for review.
Raw trace feedback_bad remains latched; feedback_hard_bad separately drives fatal
trace failure. CAN RX fields[12]=1 marks a deferred stop-band sample. Dump emits
feedback_review_summary and each feedback_review timestamp, ordinal, wheel, raw
speed, tolerance and pending verdict after trace_end. Pending never means PASS.
The owner still requires >=150ms stable feedback within1s; preflight/cleanup
standstill, active motion/direction, opposite-wheel, X1, input/CAN failure and
capture completeness gates remain enforced. No negative command is permitted.
This is scoped to stop transients, not a blanket ignore-errors mode.

Tests changed first: trace unit failed32 checks before implementation, then passed.
Debug45/45 and sanitizer45/45 passed with no skips. vcan verifies−2.1rpm and
archived stop rebound remain recorded while stop verification/restoration complete;
active wrong-axis feedback and stop timeout retain failure. Unit checks−7.6rpm
still latches hard failure. Scoped clang-analyzer/bugprone checks pass with no
emitted project diagnostics; broader default tidy warnings are retained separately.
Locked aarch64 build,109-input snapshot match and qualification ELF audit pass.
Artifact cd8bd1c594c9297a9036ff585cdc82e852f99795c3f8a8c27e6bdd453d5df7a1.
Target help and pure control-cycle smoke passed in the new A6 version directory.
A6 has fresh one-shot markers and local current-readiness gate; old trials unchanged.
