# PASS — bounded receive-only Source acceptance

6434 frames,6430 reads,6431 snapshots; two startup rejected candidates. Independent raw decoding and every snapshot mapping match. All eleven contract-aligned gates pass; the startup oracle review and original failed oracle output are retained separately.

Input authorizations1/2/3 begin at7.134455076s,25.573039376s and40.903689491s, each initially zero. Normal disable occurs16.794731282s. Nonneutral rearm is refused; returning neutral alone does not enable. Lost begins30.571170300s, failsafe31.068295080s; healthy frames return35.779850106s and recovered/disabled35.793435638s, with no auto-enable before the new press. SIGTERM inhibits the nonzero final input, emits shutdown/zero/invalid, exits143, and is reaped33.3009ms after signal (includes wrapper scheduling/log overhead; bound1s).

Operator confirmed the planned actions and final transmitterON, sticksneutral, CH6released, CH7middle. No CAN, drive command, production service or physical configuration was changed. The input snapshot is not system motion authority. The artifact, runner and this run's authorization are consumed; future runs need separate authorization.
