# F5 partial-frame handling preparation

Dedicated --zero-uart-recovery accepts exactly one empty partial_timeout batch
in phase1, preserves immediate Source revocation/session change and verifies
byte-free hold. Service-gap/backlog/RF flags/noise/transport remain fatal. No
Reader/Source policy changes. Debug46/46 and sanitizer46/46 passed; scoped static,
locked cross,109-input snapshot match and qualification ELF checks passed.
Artifact13ccbd9d target smoke passed and A2 consumed once. A2 verified this fix,
then exposed a separate phase2 parser-candidate reacquisition rejection. Full F5
recovery remains unaccepted; see zero_uart_a2. New window preparation supersedes
this observer but not the original evidence.
