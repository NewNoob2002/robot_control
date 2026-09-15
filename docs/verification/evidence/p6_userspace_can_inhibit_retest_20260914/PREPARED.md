# Userspace CAN inhibitor physical retest preparation — 2026-09-14

**PREPARATION SNAPSHOT; SUBSEQUENTLY AUTHORIZED AND CONSUMED.**

This runner reuses the already installed, hash-verified qualification and
inhibitor executables. It changes only manual coordination and evidence
handling:

1. Target candump and JCAN silent capture start before motion.
2. The target terminal asks the operator to place a hand on the RK3588 CAN
   connector and type ARMED; motion cannot start before that marker.
3. The unchanged application commands right +5 rpm, left zero, with its fixed
   200 ms lead and 8 s maximum external-loss window.
4. On DISCONNECT NOW, the operator removes only the RK3588 CAN branch, waits
   for normal stop, reconnects about 3 s later, then types RECONNECTED.
5. The runner accepts target candump's exact 'can0: interface down' exit, requires
   interface down within 4 s of the motion prompt, keeps the interface down
   after reconnect, and rejects any host request received by silent JCAN more
   than 250 ms after interface-down notification.

At this preparation point the staged authorization file had authorized=false.
The later exact authorization, consumed marker and passing result are retained
beside this snapshot in authorization.json, run_once.marker and RESULT.md.
