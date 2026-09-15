# Phase 6 soak investigation and deferral — 2026-09-15

Status: **DEFERRED BY OPERATOR; NO SUCCESSFUL LONG-DURATION SOAK**.

## V3 failed attempt

The requested three-hour run started at 11:54 on September 15 (UTC+08:00).
Its result is FAIL after 309.067 s and five successful application cycles.
The failure is `AssertionError: candump exited`; capture and executor stopped.
All 300 TPDO and 900 SDO velocity samples were zero. The retained 9215 CAN
frames pass the traffic validator, and sampled error/drop counters did not
increase. The eight archive-member hashes were verified. These partial results
do not establish three-hour endurance or complete final-cycle capture coverage.
The original bundle is retained as
[v3_failed_archive.tar.gz](evidence/p6_zero_motion_soak_v4_20260915/v3_failed_archive.tar.gz).

The last captured frame is timestamped 11:59:36.176239. Target journal records
the SSH disconnect at 11:59:36. A receive-only test of the installed can-utils
2020.11.0-1 candump inherited ignored SIGHUP, then installed its own handler
(SigIgn=0, SigCgt=0x4003). Sending SIGHUP to that test process caused exit 0
with empty stderr. No CAN data was transmitted. This reproduces the mechanism
and strongly supports terminal hangup as the cause; the old runner did not
retain candump's exit code or the actual signal sender, so those are not proven.

The runner now creates independent child sessions with detached stdin, checks
capture liveness during each cycle and before another cycle, retains its exit
code, and invokes bounded application cleanup on capture loss. New host and
RK3588 offline checks verify hangup isolation and a capture exit 0 midway through
a simulated cycle: the child is stopped in about 0.55 s and the result is FAIL.
The deliberately failing simulated run is a passing negative regression test.
No successful physical long run of this repair is claimed.

## V4 preparation and JCAN blocker

The operator authorized one replacement run and confirmed powered drive, locked
X1, raised stationary wheels and unchanged wiring. A temporary USB-node ACL
restored access to serial 207F346D5650. The normal-receive ACK session then failed
before the target runner started: a USB record declared 3072 payload bytes but
contained 3654 total bytes (3648 payload bytes), with no second outer header.
JCAN rejected it, exited with cleanup, and read back unchanged configuration.
Zero JCAN data-frame commands were submitted. USB/adapter framing root cause
remains unresolved; no parser relaxation, device reset or hardware retry ran.
See [preparation evidence](evidence/p6_zero_motion_soak_v4_20260915/RESULT.md).

The operator subsequently deferred soak until SBUS and full-chain integration
are ready. V4 has no run marker or output directory. Its unused authorization
is now false both locally and on RK3588; original preparation records remain
preserved. All older consumed runners remain consumed.

## Scheduling and acceptance

Continue later component development with short regressions. Once integrated,
run both the isolated CANopen lifecycle and full-chain long-duration checks
under new artifact/preflight authorization. Soak is not waived and Phase 6
remains open. The last recorded preparation state was powered/X1 locked with
can0 UP; subsequent power-off is not confirmed. No hardware action is implied
by this archive or its CI.
