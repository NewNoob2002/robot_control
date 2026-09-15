# P6 drive-only power loss/restoration v2 — prepared 2026-09-14

Status: **PREPARED, NOT AUTHORIZED, NOT RUN**.

V1 is an invalid historical attempt: the application reached its eight-second
no-loss deadline and sent zero before the operator removed drive power. Its
silent JCAN capture later exceeded 100000 frames because the powered-on drive
repeated boot heartbeat `0x701` without an ACKing node.

V2 retains the same tested qualification ELF, userspace inhibitor, one right
`+5 rpm` target, left zero, absolute motion bound and final drive-power-off. It
changes only the independent JCAN observer from `silent` to `normal --receive`.
The adapter therefore acknowledges valid frames at the CAN link layer but the
runner submits no JCAN data-frame send command. The operator must watch the
target terminal after typing `ARMED` and remove drive power immediately without
waiting for a chat update.

Acceptance requires a post-motion drive boot, the first RK3588 request after
that boot to be packed zero, verified zero/disabled cleanup, no CAN error, no
motion reauthorization, at least five seconds of operator-confirmed no restart,
unchanged JCAN configuration, final drive power OFF and a powered-off final
`can0` DOWN step. Any other outcome fails without retry.
