# Online startup correction — 2026-09-11

The drive may start before RK3588. Historical boot-up is not a readiness
requirement. ObservationStore already accepts fresh ordinary heartbeat with
boot_observed=false; an actual later boot remains a generation change that
invalidates an active sequence. No synthetic boot or generation is introduced.

The earlier review-motion preflight claiming each round required a power cycle
was incorrect and was withdrawn before physical execution. Its manifest remains
marked withdrawn for traceability. Do not use its historical boot helpers.

Zero-sequence and first-motion now use the existing online preparation when no
fresh heartbeat is present: read 0x1017:00, require the reviewed zero baseline,
write/read back volatile 500 ms, and wait for a genuinely newer heartbeat. The
same preparation is shared with stop trials. A nonzero but silent heartbeat
configuration is rejected rather than overwritten. Already fresh heartbeat is
accepted without a boot-up and without changing its setting.

The CLI waits at most one second for existing online observations before the
bounded SDO probe, replacing the obsolete 180-second startup wait and misleading
wait_boot diagnostic. Motion still requires live TPDO contract, zero targets,
zero speed, mode 3, no fault, current feedback and newer matching states at each
transition. Heartbeat preparation grants no motion authority.

Owned temporary heartbeat is restored on every exit; a restore failure is
reported and is not automatically retried after motion cleanup. All SDO/NMT and
cleanup are sent by the RK3588 application. No external boot helper, cansend,
JCAN transmission, reset command or operator power cycle is needed.

Regression extends the existing zero/first-motion cases with three successful
sessions having no boot-up and initially no heartbeat. Both entry paths also
reject silent nonzero producer settings, missing new heartbeat, bad mode, and
failed restoration. They prove no extra activation frame and retain
boot_observed=false. The old implementation failed the new online checks and
its scripted responder reached the 60-second test timeout; the corrected full
host suite passes 57/57. Final sanitizer/cross/target results are in the evidence
folder, not inferred from earlier builds.

Evidence: [online startup validation](evidence/p6_review_online_20260911/).
Physical zero/left/right requalification remains pending on this new artifact.

Final validation: host 57/57, ASan/UBSan 57/57; aarch64 Debug build/ELF audit and actual RK3588 namespace-vcan pass. Scoped clang-tidy exits 0 with 0 errors and 43 advisories. No physical CAN operation occurred.
