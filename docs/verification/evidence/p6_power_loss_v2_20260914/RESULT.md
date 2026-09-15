# P6 drive-only power loss/restoration V2 — 2026-09-14

Disposition: **PHYSICAL PASS WITH A POST-RUN TEST-HARNESS ASSERTION DEFECT**.

The single authorized raised-wheel trial used the existing Debug-only
qualification ELF and userspace `can0` inhibitor. JCAN serial `207F346D5650`
ran in `normal --receive` mode so its controller supplied CAN ACK. The runner
submitted no JCAN data-frame command; its only JSONL request was session
shutdown. This active-ACK observer choice is specific to isolating drive-power
loss from the separately qualified CAN-error/inhibitor path.

RK3588 candump and JCAN contain the same 352 frames in the same order. The
application sent one right `+5 rpm` packed target. Seventy-four TPDO1 samples
showed right-axis motion; every left-axis speed sample remained zero. No CAN
error was observed.

After the drive-power interruption and restoration, a new boot heartbeat was
captured at `1789381482.921785`. The first RK3588 request followed 2.494 ms later
and was packed zero (`601#23FF600300000000`). No later nonzero target, NMT
Operational or Enable Operation request occurred. Cleanup reached raw dual
status `0x14401440`, all three speed views were zero, restored `0x2000:00=0` and
`0x1017:00=0`, released the inhibitor and exited 0.

The operator completed the power-off/restoration/final-power-off sequence,
entered `NO_RESTART`, and subsequently confirmed no abnormal behavior in
response to the right-stop, left-stationary, restart, sound and final-site
checks. Final powered-off cleanup leaves `can0` DOWN/STOPPED with no residual
qualification or capture process. JCAN configuration is unchanged.

The target wrapper and outer coordinator returned failure only because
`remote_trial.py` asserted that the internal successful `Status` context must
appear in stdout. The executable intentionally prints that context only on
failure; on success it printed
`interface_inhibitor_released interface=can0 cleanup=verified` and
`qualification_complete node=1 operation=13`. The independent frozen-evidence
validator `analyze_result.py` checks the exact captures, application exit,
operator records and final cleanup and passes. The original false wrapper result
is preserved and no hardware retry was performed.
