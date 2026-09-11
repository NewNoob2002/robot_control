# Running-speed object discrimination — 2026-09-11

One authorized left-axis +5 rpm / 3000 ms trial completed. It kept the existing
status-first TPDO1 mapping, temporarily selected 0x200F=0, and restored 1.
All six running sample groups show nonzero independent left speed, zero
independent right speed, and zero packed speed read directly through SDO.
Nearest TPDO speed halves are also zero.

| First SDO request after target (ms) | 606C:01 raw | 606C:02 raw | 606C:03 raw | Nearest TPDO speed halves |
| ---: | ---: | ---: | ---: | --- |
| 102.992 | 50 | 0 | 0 | 0 / 0 |
| 603.994 | 46 | 0 | 0 | 0 / 0 |
| 1114.993 | 46 | 0 | 0 | 0 / 0 |
| 1625.995 | 53 | 0 | 0 | 0 / 0 |
| 2125.994 | 48 | 0 | 0 | 0 / 0 |
| 2625.998 | 48 | 0 | 0 | 0 / 0 |

The local vendor dictionary specifies 0.1 r/min for these feedback objects,
so the independent samples correspond to 4.6–5.3 rpm. Each three-object group
spans about 4 ms between first and last responses; these are sequential reads,
not simultaneous values. The repeated steady-motion observations avoid the
previous cleanup-only sampling limitation. Exact raw source lines and nearest
TPDO offsets are retained in left/analysis.json.

## Conclusion and limits

This isolates the discrepancy to the drive combined-velocity object (0x606C:03)
under the tested asynchronous, enabled-drive conditions. A host decoding error
or TPDO transmission-only defect cannot explain the independently uploaded
zero value of that object. TPDO agrees with its mapped object in this trial.

The result supports the proposed command-mode/combined-feedback association,
but does not prove that 0x200F alone causes it: enabled state also differs from
the accepted manual test. No synchronous-mode powered comparison or controlled
disabled-drive 0x200F A/B test was performed. Do not assert that the manufacturer
documents 0x606C:03 as synchronous-only, or that this configuration is generally
safe for motion-feedback supervision.

The next discriminating test would compare disabled manual rotation with only
0x200F changed, or validate independent 0x606C:01/02 PDO mapping. Either requires
its own reviewed application sequence and current hardware scope. Neither was
executed automatically. Do not infer that changing a single mode bit permits
reusing the asynchronous target-command sequence in synchronous mode.

## Execution and cleanup

- RK3588 application and coordinator exited 0. One nonzero target, no retry.
- First zero request followed the target by 3000.998 ms; this is a measured
  request interval, not a hard real-time guarantee or mechanical stopping time.
- Both captures match all 256 frames in exact order. All 79 SDO transactions
  match: 64 uploads, 15 downloads; there are two NMT requests.
- Kernel counters reconcile exactly: target TX 81 packets / 636 bytes,
  RX 175 packets / 1257 bytes. CAN remains ERROR-ACTIVE, with zero errors/drops.
- Final targets, all velocity views and fault value are zero; both status halves
  are Ready to Switch On; Pre-operational heartbeat was observed. Producer
  heartbeat is restored to zero and 0x200F to 1. Processes stopped normally.
- JCAN 207F346D5650 was silent throughout; coordinator verified unchanged
  adapter configuration. No EEPROM or PDO mapping writes occurred.
- On 2026-09-11 the operator confirmed this specific trial: only the left wheel
  rotated and stopped normally, the right wheel stayed stationary, and there
  was no abnormal sound. Direction was not specified. See
  left/operator_observation.json. Packed-feedback qualification and mode-causality
  isolation remain open.

## Software verification

The Debug-only qualification target interval now reads 0x606C:01/02/03 after
100 ms and then no faster than every 500 ms, at most six groups in three
seconds. No new CLI, generic transmit API or permission was added. Each upload
has a 20 ms timeout bounded by the original target deadline. A group starts
only with more than 100 ms remaining; upload failure immediately reaches the
existing zero and cleanup path. Existing owner/generation/heartbeat/TPDO/state
checks remain active. No logging is added to the motion loop. Raw captures
are the diagnostic output; zero exit does not mean feedback tracking passed.

- New responder cases reproduce the missing sampling against the old binary
  (regression_red.log) and pass with the change (narrow.log). They cover a
  nonzero independent/zero packed response, missing response and SDO abort,
  including early zero on failure and fixture restoration.
- Host: narrow vcan pass plus other 56/56 tests. Sanitizer: 57/57 pass.
- Clean aarch64 Debug build with pinned GCC11.4 image and target sysroot; both
  application and target test ELF dependency/version/RPATH audits passed.
- Target namespace-local vcan passed, prohibited=0 and failures=0, no skip.
- LLVM22 clang-analyzer/bugprone checks: zero errors, eight advisories outside
  the added sampling and test blocks. The initial sanitizer database invocation
  failed on generated module-map paths; its log is retained, and the successful
  host-database invocation is in clang_tidy_host.log.
- Default/Release/commissioning suites were not rerun: no code in their build
  paths or target-selection gates changed. The qualification gate test passed.
- git diff --check passed. No production deployment or full Phase6 acceptance.

Artifact SHA256: 401305bd8b2087f8acf461c1a12de9dcdac4982902e93207c1f338368ce727f8.
Manifest binds target files and changed source hashes. Reproduce offline with
`python3 docs/verification/evidence/p6_tpdo_probe_20260911/analyze.py`; do not
rerun left/local_trial.py, whose one-shot marker is deliberately retained.
