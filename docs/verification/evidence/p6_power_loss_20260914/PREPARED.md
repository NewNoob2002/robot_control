# P6 drive-only power loss/restoration — prepared 2026-09-14

Status: **PREPARED, NOT AUTHORIZED, NOT RUN**.

This one-shot raised-wheel runner reuses the physically validated qualification
ELF and userspace `can0` inhibitor. It commands only right wheel `+5 rpm`, keeps
left wheel zero, and prompts the operator to remove only drive power. After at
least three seconds powered off, it restores drive power once and observes at
least five seconds for no restart before requiring final drive power-off.

Two fail-safe outcomes are accepted by the validator:

1. no CAN error: a post-motion drive boot is observed, the first RK3588 request
   after boot is packed zero, cleanup is verified, and no motion is reauthorized;
2. CAN error: the inhibitor leaves `can0` DOWN and no RK3588 request follows the
   first error, while silent JCAN still observes the drive boot after power is
   restored.

The run requires target candump plus passive JCAN serial `207F346D5650`. It has
one attempt, no automatic retry, no JCAN transmission, no persistent drive
write, and no kernel or SDK change. The final condition is drive power OFF; a
separate powered-off sudo step leaves `can0` DOWN if clean recovery had left it
UP.
