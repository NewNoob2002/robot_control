# Explicit ±2 rpm standstill criterion (2026-09-17)

User requested “再次增加这个启停波动的转速阈值到+-2rpm”. Control HIL now defaults
to --zero-feedback-tenths-rpm 20; explicit values 0..20 are accepted. Runtime,
qualification and trace library defaults remain exact zero. Startup, stop and
cleanup use the explicit band; positive target/feedback bounds, zero targets,
>=150ms standstill hold and loss/rearm requirements remain unchanged.
Historical motion analysis defaults to15; this revision explicitly passes20.

Verification: Debug45/45 and ASan/UBSan45/45 passed, including isolated vcan
tests (no skips). ±20 accepted/±21 rejected boundaries and historical15 are
covered. Scoped clang-tidy passed with no emitted project diagnostics.
Locked aarch64 cross build and qualification ELF audit passed;109 project
C/C++ inputs match the source snapshot. Artifact SHA256:
b8f9192fc66a71a3e854e5e1426ff618ed5d4346ff44acf1da54ca2ec8221c58.
No target deployment, target smoke or new physical trial was performed for
this artifact; target execution of the new default remains unverified.

A5 original artifact38800cfe used CLI15 and remains FAILED under that criterion:
one post-stop sample−1.6rpm exceeded±1.5rpm. Original hashes, raw trace and
feedback_bad=1 are preserved. analyze-a5.py verifies the original SHA256SUMS,
reproduces the old failure and passes the explicitly revised20 analysis.
Moving flags0→4 occurred at7089.866087ms, before7950ms cutoff; application
receive→zero send22.167us, no subsequent nonzero or X1.2313 dual-capture
frames match,1356 RPDOs,36 volatile writes and full baseline restoration.
Revised stable-band verification completes275.795564ms after first zero.
Operator confirmed OFF, normal right direction/stop, left stationary,
no abnormal sound and receiver powered throughout.

F4 A5 is accepted under the explicitly revised±2rpm criterion by retrospective
analysis, not by running the new binary. Original failed/consumed trials stay
unchanged. F5/F6 remain pending and P10.3 remains OPEN.

Reproduce evidence analysis: python3 -B
docs/verification/evidence/p10_3_fault_recovery_20260917/standstill_20_revision/analyze-a5.py
Full test commands: ctest --test-dir out/build/p103-control-hil --output-on-failure
and ctest --test-dir out/build/p103-control-hil-san --output-on-failure
(see XML/logs for actual build paths and results).
