# Long-soak prerequisites — offline preparation, 2026-09-17

Started after P10.3 delivery commit55617ec passed Actions run35216344992.
No physical interface, JCAN session or soak was started. Earlier consumed runners
and retired v4 authorization remain unusable. P6 remains OPEN.

## Findings and completed work

- Current JCAN checkout b9d39f2 is unchanged. Its software self-test passes
  (`jcan-self-test.json`); this is not USB/capture reliability evidence.
- The archived v4 bad USB packet has a valid-looking outer prefix but declares
  3072 body bytes while containing3648. The body has no FF AA inner-frame
  preamble. Current receive() still rejects the length mismatch. The independent
  envelope audit records original/source hashes in `jcan-envelope-audit.json`.
  It does not replay a real USB transfer or identify the physical cause. Simply
  accepting trailing bytes would not demonstrate recovery of valid CAN records.
- Existing SIGHUP isolation and premature-candump-exit regressions pass.
  A new regression exposed unbounded waiting after stop if the executor ignored
  SIGTERM. The monitor now leaves its loop and enters the existing bounded
  TERM/wait7s/KILL cleanup. A real fake executor ignoring TERM is reaped in
  7.575s; capture also stops, zero cycles are accepted and status is INTERRUPTED.
  See `session-regression.log`. Process exit alone never proves physical zero
  or drive power OFF. No target deployment or target smoke is claimed.

## Gates before a new soak

1. Diagnose the malformed JCAN envelope using its archived fixture and the
   adapter protocol/firmware contract. Preserve rejection of corruption and
   framing loss; verify the fix offline before any new capture.
2. Review a fresh bounded capture preflight with explicit adapter identity,
   bus mode and duration. The old failing capture used normal ACK mode; silent
   short captures do not qualify that mode. Obtain current authorization before
   changing bus mode or starting a physical session. Compare against target
   candump; disconnect/reconnect or either capture exiting invalidates the window.
3. Define the new integrated soak workload and artifact. The old zero-motion
   Phase6 runner is not an SBUS/ControlLoop soak. Add a workload-derived cycle
   watchdog: its existing per-cycle loop still has no autonomous wall deadline
   absent a stop request or capture failure. Verify SSH/HUP, operator stop,
   capture loss and stalled executor cleanup with the selected artifact.
4. Review current zero/layout/heartbeat/SBUS readiness, unloaded raised wheels,
   physical X1/power-off path, userspace inhibitor and kernel retry-worker limits.
   Rebuild/hash and perform device-free target smoke, then request fresh powered
   readiness and one-shot authorization for the exact short trial/soak duration.

The offline session regression passes; JCAN reliability, new workload/watchdog
and fresh physical readiness remain gates. No successful three-hour soak or
production acceptance is claimed.
