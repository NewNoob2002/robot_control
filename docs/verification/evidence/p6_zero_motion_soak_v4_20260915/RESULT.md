# V4 soak disposition — 2026-09-15

**DEFERRED / NOT STARTED. JCAN preparation failed; no replacement soak ran.**

- Host and RK3588 offline runner tests pass, including SSH/SIGHUP session
  isolation and prompt cleanup after simulated capture exit 0.
- JCAN serial 207F346D5650 entered normal receive mode, then rejected a USB
  packet with 3072 declared versus 3648 actual payload bytes. Its keeper
  recorded the raw error and exited; configuration before/after is identical.
- JCAN data-frame commands: zero. The target one-shot marker and soak output
  directory were absent when authorization was retired.
- Operator deferred soak until SBUS/full-chain work is ready. Current
  authorization.json is disabled; *.prepared.json preserves original records.
- Target retirement was verified at 2026-09-15T10:13:31.978127Z, with no physical
  interface change. Authorization SHA256:
  30b885a2d93793b5004cf5d2e1e78e7a3efdd2dc5c233be11fb50b20f698b59f.
- No later drive power-off confirmation is recorded.

The retained v3_failed_archive.tar.gz is the earlier failed target run, not a
V4 result. See [investigation](../../P6_SOAK_SESSION_REPAIR.md).
