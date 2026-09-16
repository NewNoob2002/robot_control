# P9.3 evidence — 2026-09-16

- software/: final host Debug/Release and ASan/UBSan logs (33/33 each, no skips), locked cross metadata/log, final scoped static checks, initial static findings, observer ABI audit and final source hashes.
- initial_target_smoke/: initial pure-contract target executable, SHA256 74553f71720d6e65892a21d54d1de8b7ad6ab373df457e7a6553b47b2e12d820, passed without device access. This predates the stop/CLI addition; it is not the final Source HIL artifact.
- calibration/: one newly authorized 30-second receive-only run, using the previously accepted raw observer bd76d3fd…; 4284 healthy frames, independent decoding, target-timed operator prompts and confirmation. Endpoints/signs and initial neutral medians establish the recorded observation profile; transient stick movements remain in the raw log.

Source live acceptance passed in attempt2: 6434 frames, 6431 snapshots, three fresh authorizations, recovery without auto-enable, SIGTERM zero/invalid within 33.3009 ms and operator confirmation. See source_hil_attempt2/DISPOSITION.md and GATE_REVIEW.md for the startup oracle correction and preserved original report/analyzer. Attempt1 remains FAILED in source_hil_attempt1/. All captured authorizations/runners are consumed historical evidence only. No CAN or motion authority is included.

The software source manifest is checked from the repository root; SHA256SUMS is checked from this directory. The build identity is a dirty development snapshot, not a clean Release claim. Historical P9.2 traces are replayed in CTest but are not copied or reclassified here.
