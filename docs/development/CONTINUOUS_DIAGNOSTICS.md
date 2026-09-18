# Continuous zero-soak diagnostics

Implemented2026-09-18, Debug-only/default-OFF with the control HIL build.

## EasyLogger and the system journal

Reuse service/logging/Logger for structured, low-rate output. EasyLogger writes
to stderr; a future systemd service may collect that stream in journald. These
are complementary responsibilities, not alternative control authorities. This
qualification uses diagnostics.log directly, without installing a service or
changing system journal settings. No new logging dependency is introduced.

The control owner must not call the mutex/string-formatting logger. In the
explicit --zero-soak mode, Trace publishes fixed-size records into a borrowed
nonblocking FIFO. Each152-byte record fits the POSIX minimum atomic pipe-write
bound; there is no retry, wait, allocation or string formatting on this path.
A separate robot-control-diagnostics process retains the raw bytes on stdout and
formats EasyLogger status on stderr. A full/broken pipe latches evidence failure;
the existing owner stop/restore path runs. The consumer's failure is independently
checked by the supervisor. A stalled disk can stall the consumer, but cannot
make the owner wait for that consumer. This is not a hard-real-time guarantee.

Existing short HIL modes keep their preallocated bounded trace and post-stop
export. Only zero-soak can attach the streaming pipe, validated as writable FIFO
with O_NONBLOCK before any UART/CAN access. Its independent send gate remains
zero-target-only. CLI range is2000..3600000ms; no motion-window expansion and no
five-hour mode is enabled. Initial authorization must arrive within60seconds;
authority loss after enable terminates the session without automatic rearm.

## Outputs and live observation

- diagnostics.log: roughly1Hz control_timing, input_control, sbus_raw,
  capture_counts and drive_state; immediate lifecycle stop/end records.
- trace.bin: all ordered raw Source/batch/cycle/CAN-attempt observations,
  independent of the1Hz display. Collector verifies ordinal continuity and
  requires header/footer and EOF. Source receive times remain distinct from
  snapshot publication times; a TX syscall is not proof of wire delivery.
- application.log: preparation, control start, stop reason and separate
  primary/restoration results. Continuous telemetry does not replace these.
- progress.json/resources.jsonl: supervisor elapsed time, CPU usage, RSS,
  threads/FDs, free disk, capture size and sampled CAN state/error counters.
- candump.log plus independent JCAN: the physical-wire comparison remains
  mandatory for accepted target evidence; application trace is not independent.

During a target trial use tail -f diagnostics.log or read progress.json in its
new staging result directory. A stale file is not proof that the process is
alive: the supervisor requires fresh data, capture processes and a live host
lease. DONE rc0 only means ready for offline evidence audit; operator stationary/
no-sound/OFF and dual-capture checks are still required for final acceptance.

Reason and state values are enums from domain/drive/runtime.hpp and
input/sbus/runtime; raw status/fault/mode and unsigned protocol values are
retained. Ages use owner steady-clock microseconds, -1 for unknown/future stamps.
Final finished=1 state has healthy=0 and unknown ages after owner detachment;
it is not fabricated final drive feedback. Actual restored state is in the
qualification readbacks and wire records.

## Streaming format1

Little-endian19 signed64-bit integers per152-byte packet:
ordinal, kind, steady-clock ns, then16 fields. No native-struct padding is stored.
Kinds0..6 retain P10_3_TRACE_SCHEMA meanings. Added kinds:

| Kind | Fields |
| --- | --- |
|7 timing|cycles, missed periods, max lateness us, max cycle us, elapsed ms, requested duration ms, shutdown us, finished, primary errno, cleanup errno|
|8 state|Source fault, RuntimeReason, status raw, fault raw, mode raw, left/right tenths rpm, Source/HB/TPDO/diagnostic ages us, armed, healthy, epoch, authorization, finished|
|9 end|overflow, hard feedback failure, standstill tenths rpm|
|10 header|version1, standstill tenths rpm|

The collector keeps constant memory; no hour-sized vector or trace concatenation.
Each EasyLogger context is capped below its512-byte configured line limit.
Overflow, missing/truncated records, extra data after footer, failed raw/log sinks
or missing input for15seconds reject collection. Header/footer validity alone
does not mean the control test passed: inspect primary/cleanup and offline audit.

## Verification and trial bounds

Python collector tests cover framing, truncation, ordinal gaps, /dev/full and
full stderr. Trace tests cover streaming beyond an in-memory fixture capacity,
pipe saturation and bounded failure. Real vcan/PTY tests exercise the unchanged
zero-target owner, nonzero rejection, X1, SIGTERM, missing diagnostics, and
collector death/suspension with verified restoration. The independent streaming
analyzer checks raw-to-candidate linkage, zero targets and all captured TPDO
feedback with±2rpm; it reads one packet at a time.

Target supervision is scripts/hil/control_zero_soak.py. It checks target/ELF
identities, consumes a fresh output directory, monitors an explicit config and
requires a host lease every10seconds. No interface configuration or persistent
parameter changes are added. Shutdown gets15seconds restoration grace before
forced process termination; forced termination is always failed/unverified.
Raw evidence, counters and failure disposition survive unsuccessful trials.

This software change prepares G1/G2. Real target acceptance must be recorded
separately; it does not reopen old runners or certify ground motion.
