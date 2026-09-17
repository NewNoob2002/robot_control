# F5 longer coordination and reconnect acquisition (2026-09-17)

User reports intervals/window too short after A2. F5 alone now permits120s total,
minimum5s byte-free disconnect hold and45s phase2 reconnect/challenge deadline.
Initial rejected parser candidates are recorded while Source stays revoked; the
observer waits for healthy Disabled input and emits uart_reconnected/INPUT_RESTORED
before asking for the nonneutralCH6 challenge. No Source/Reader/production policy
change. Later rejected data, additional discontinuities and hard faults still fail.
No nonzero target or automatic recovery authorization is introduced.

Tests updated first and failed the old hold. Debug46/46 and ASan/UBSan46/46 pass,
no skips; F5 cases cover partial/clean/rejected-first-frame recovery,RF-loss/noise/
backlog/service-gap rejection,unchanged legacy F2 partial rejection,actual45s
reconnect deadline and120000/120001ms CLI bounds. Scoped static,locked cross,
109-input snapshot comparison and qualification ELF audit passed. Artifact:
bcd4cfe96b1e11b496a18fbe39b229764510f685ef29c5c0334a797690ef5dc4.
No new target deployment/smoke/HIL was run for this artifact. A3 local scripts are
prepared with authorized=false; fresh physical authorization/readiness required.
A2 remains incomplete with full raw evidence and successful baseline restoration.
