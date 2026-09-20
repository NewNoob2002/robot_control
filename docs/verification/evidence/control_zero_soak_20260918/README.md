# Continuous SBUS/control zero-soak trials — 2026-09-18

[Short A1](short_a1/README.md) failed during capture startup and remains consumed.
[Short A2](short_a2/README.md) passes its 600-second scope.
[One-hour A2](hour_a2/README.md) completed 3600.367 seconds: functional checks
pass, but strict global capture ordering fails in one three-frame window.
Per-ID/per-direction sequences match; the user accepted this disclosed exception on 2026-09-18. G2 zero-target endurance is ACCEPTED; original strict failure remains.
Operator OFF/stationary/no sound is recorded for each completed trial.
The capture-startup repair reuses the already qualified Phase6 capture helper;
inert red/green tests and receive-only target smoke are in capture_startup_fix.
No C/C++ or artifact change: control-hil bec74e79 and diagnostics9a226adb remain
as software-verified in ../control_diagnostics_20260918. Future attempts require
new directories and fresh local START; this index is not a retry authorization.
