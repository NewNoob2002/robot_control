# P6 X1 emergency-input zero-motion pretest V2

Status: **AUTHORIZED ON 2026-09-15, NOT RUN**. Target Python syntax, ELF hash, argument gate, and unchanged-DOWN-interface/TX checks pass. The repaired ELF is `root:cat`, mode `750`, with the expected SHA-256. `can0` is DOWN/STOPPED with zero current bus-error counters, and one V2 attempt is authorized.

Attempt 1 stopped before the X1 stimulus because packed status `0x14001400` was falsely rejected as enabled. V2 uses repaired ELF SHA-256 `bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7`; its guard accepts CiA402 Not Ready to Switch On mask `0x00` as a non-enabled state while retaining all previous rejection states.

The physical scope is otherwise unchanged: raised and stopped wheels, no target/RPDO/controlword, silent JCAN serial `207F346D5650`, X1 locked for about three seconds then reset, five-second no-motion observation, bit 15 `0→1→0` on both status halves, zero velocity throughout, one attempt only, and final drive power OFF with `can0` DOWN.

Software evidence before target staging: host and ASan/UBSan 68/68, default build 28/28, focused managed-vcan pass with zero prohibited frames, RK3588 cross build and ELF audit pass.
