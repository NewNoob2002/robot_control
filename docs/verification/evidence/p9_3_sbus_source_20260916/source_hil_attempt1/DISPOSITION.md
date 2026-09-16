# FAILED — first Source HIL attempt

The operator explicitly requested retaining this run as failed and starting a new test.
6433 frames,6429 reads,6430 snapshots; raw decoding and all mapping comparisons match.
Lost/failsafe inhibition and shutdown zero passed, but the requested startup-held/enable/disable/nonneutral sequence was not observed as planned: CH6 stayed200 until21.625761s; authorizations were1 and2 rather than1,2,3.
Do not modify the original failed gate report or classify this as complete acceptance. The recording and Source mapping checks are valid evidence; the failed planned manual gates remain failed.
SIGTERM exit143,32.119712ms signal-to-reap; final pre-shutdown command19/19 rpm was a diagnostic snapshot only, followed by invalid zero/shutdown. No CAN or drive output occurred.
This runner and its authorization are consumed. A new operator-authorized attempt uses a different staging/output directory and longer preparatory countdown; no production code change is justified by missing raw manual transitions.
