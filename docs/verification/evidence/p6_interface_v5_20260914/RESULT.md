# Interface-loss v5 — 2026-09-14

The single physical trial PASSES with operator acceptance. The user requested a longer motion window and a
new v5 trial after v4 ended without an interface-loss stimulus. V4 remains a
consumed failed qualification with verified zero-speed cleanup and operator
confirmation; no previous result is relabeled.

## Revised scope

- Same RK3588, node 1, can0 at 500 kbit/s; unloaded raised wheels, right +5 rpm,
  left zero; one nonzero packed target only.
- The application computes an absolute steady-clock deadline before motion,
  eight seconds later. No loss by that deadline enters zero/Shutdown cleanup.
  This bounds the request interval, not a guarantee of mechanical stopping at
  exactly eight seconds; scheduling and deceleration remain observable limits.
- Operator Enter must follow actual right-wheel motion and an application arm.
  The terminal accepts arm age 0 through less than 6 seconds, reserving time
  before the application deadline. A late arm or missing motion is rejected.
- One can0 down, three-second hold and up in finally. Passive recovery remains
  ten seconds; first resumed request is packed zero, followed by zero/disabled
  verification and volatile baseline restoration. No automatic re-enable.
- Unchanged 1000 ms drive watchdog, 500 ms heartbeat and 100 ms TPDO freshness,
  preflight, one-shot markers, 40-second/100000-frame dual capture, and CAN
  request allowlist. No cable/power trial or persistent configuration write.

## Software evidence

Host build passes. The narrow qualification vcan CTest passes in 81.16 s,
including delayed external loss at 6 seconds and absent-loss zero request at
7.8–8.3 seconds on the simulated peer. Remaining host CTests pass 61/61, for
62/62 total. ASan/UBSan CTests pass 62/62. Existing loss, signal, missing recovery,
and first-zero-failure scenarios remain covered by the same suite.

Clean pinned cross builds pass for qualification, default Debug and default
Release, with ELF audits. Source snapshot attestation and exact ELF/source
hashes are in source-attestation.json and manifest.json. Stage results prove
remote file hashes. RK3588 isolated-vcan passes with 18127 monitored frames,
prohibited=0 and failures=0, and empty stderr. This did not access physical can0.

Scoped clang-tidy (clang-analyzer, bugprone, performance) exits zero with
advisories in existing code; the changed deadline adds no reported diagnostic.
An initial invocation without enabled checks failed and is retained. The
analysis-only compile database omits CMake module response-file arguments;
production compile options are unchanged.

Offline helper checks accept 100000 frames and reject 100001, exercise normal
and interrupted hold/down-failure/up-failure cleanup, and evaluate the actual
arm guard with fresh, delayed, expired, future and missing-motion inputs.

## Physical result

The newly authorized runner executed once after fresh local sudo-terminal READY.
The target-to-interface-down interval was 1378.299 ms, and the helper held the
interface down for 3003.580 ms before beginning restoration. The application and
coordinator exited zero. The first resumed request was packed zero, 53.913 ms
after up began (49.897 ms after the helper reported up complete). Fresh speed
views 1/2/3 were all zero by 567.175 ms after up began; final dual status was
0x1460/0x1460. The drive heartbeat/watchdog baselines were read back as zero.
No subsequent enable or Operational command occurred. The operator reported no
abnormality and terminal DONE in response to the full stop/site-safety question.

Target capture has 221 frames, all matched in order in the 11525-frame JCAN
capture. All non-TPDO1 frames are identical between captures. JCAN additionally
records 11304 identical TPDO1 frames, payload 2714274400002e00. Its timestamps
are zero, so neither their timing nor a physical retransmission cause is proven.
The disconnected target capture cannot establish exact stopping time during
loss. The larger capture bound was not exceeded. Final can0 is UP/ERROR-ACTIVE,
500 kbit/s, with zero error/drop counters; JCAN configuration is unchanged.

There are 299 receive-buffer initialization log lines from bounded reopen
attempts. This diagnostic rate issue remains for follow-up; no source change or
physical retry was made after the accepted run. It does not erase the verified
protocol/cleanup result or qualify remaining cable/power/fault/soak gates.

Reproduce the offline audit with:

    python3 docs/verification/evidence/p6_interface_v5_20260914/analyze_physical.py

See physical_result.json, operator_observation.json and both original captures.

## Consumed execution gate

The user's v5 authorization is recorded in authorization.json. No further
motion authorization was needed for that exact one-shot scope. The local and
remote runners are now consumed and cannot be reused. Any new physical trial
requires its own authorization; no cable/power stimulus was executed.
