# P6 NMT Stop feedback and cleanup repair — 2026-09-11

**PASS: software, RK3588 isolated vcan, one physical trial and operator acceptance.**
Phase 6/P6.7 remain open for their other requirements. This trial is consumed;
no new motion, repetition or automatic re-enable is authorized by this record.

## Defect and change

The preceding [147-frame failure](P6_ZERO_TARGET_RECOVERY.md) remains a failed
trial. The NMT waiter rejected the cached nonzero TPDO from before NMT Stop,
so Pre-operational followed Stop after only 0.149 ms. Generic cleanup then
waited for a newer PDO state even though Pre-operational suppresses PDOs.

Bounded Stop/recovery waits now explicitly allow deceleration while requiring
a matching heartbeat newer than the command and a clean generation. All startup
callers retain the zero-velocity check. Recovery stays Pre-operational, clears
and reads back the packed zero target, waits for the new Pre-operational
heartbeat, and issues one Disable Voltage. Correlated, deadline-bounded SDOs
must show both status halves Switch On Disabled and all three speeds zero.
Fault, abort, timeout and missing heartbeat remain failures. This path owns
cleanup; generic cleanup cannot repeat it or return the drive to Operational.
Temporary heartbeat production is restored to its original zero value.
No transmit-gate expansion, persistent setting, dependency or production
service was added. Qualification remains Debug-only/default-OFF.

## Software verification

The [manifest](evidence/p6_nmt_stop_repair_20260911/manifest.json) records exact
tested source and ELF hashes. After HIL, two public-header documentation lines
were corrected (two-second NMT duration limit and Pre-operational SDO cleanup);
the manifest retains both hashes and the comment-only diff. No declaration or
implementation changed. Tested source snapshot archive SHA256:
eaeee2e614352a47770687102bdded57fba5b87be2b03a33109cc9ef175950ad.

| Check | Result |
| --- | --- |
| Host qualification and managed vcan | 60/60, no skips |
| ASan/UBSan qualification and managed vcan | 60/60, no skips |
| Default host Debug / Release | 28/28 each |
| P5.6 commissioning regression | 36/36 |
| Clean pinned GCC 11.4 RK3588 builds | Qualification Debug and default Debug/Release pass |
| Locked target sysroot ELF audits | Pass |
| Scoped LLVM 22.1.8 clang-tidy | Exit 0; 19 existing advisories, no errors or new NMT-path finding |
| RK3588 namespace-isolated vcan | 12826 frames, prohibited=0, failures=0 |

The managed-vcan peer retains nonzero speed before Stop, delays Stopped
heartbeat, and emits no PDO after Stop. Seven cases cover success, missing
heartbeat with verified cleanup, drive fault, nonzero-to-zero SDO speed,
mismatched status halves, SDO abort and SDO timeout without repeated inhibit.
Startup and other stop/loss regressions remain in the full suite.
The initial red test reproduces premature Pre-operational in the old code.
An initial timeout fixture slept 250 ms despite a 30 ms SDO timeout and also
blocked heartbeat restoration. Both failed suites are preserved; the fixture
now withholds only the chosen reply and services the next request.
Production timeouts were not changed.

[Test results](evidence/p6_nmt_stop_repair_20260911/test_result.json) index the logs.
The narrow command was:

    bash scripts/test/test_socketcan_vcan.sh out/build/review-fixes/tests/unit/robot_control_canopen_qualification_vcan_tests

Full suites used:

    ctest --test-dir out/build/review-fixes --output-on-failure
    ctest --test-dir out/build/review-fixes-sanitizer --output-on-failure

The retained cross_runner.py/cross_build.py record the locked offline container
configuration; restore them to the original paths listed in test_result.json
and select fresh snapshot/build directories before another build.

## One physical trial

The user authorized repair, verification and this hardware test, then confirmed
the same RK3588/node1, raised unloaded wheels, an on-site operator, an available
emergency stop and initially stopped wheels. The new runner used right +5 rpm,
left zero, 1000 ms, then one NMT Stop.

ELF SHA256: 202bab381749c0418a61d727a7de02490d4a625594bba3e98346c6f96d3f175d.
Staging directory: /tmp/robot-control-qualifications/nmt-repair-202bab381749.
Target candump and JCAN 207F346D5650 in silent mode captured the same 140
frames and 49 completed SDO transactions. One nonzero target, one NMT Stop and
one cleanup Disable Voltage occurred; no Operational or enable followed Stop.
No startup recovery Disable Voltage was needed.

| Observation | Result |
| --- | --- |
| Target to NMT Stop | 1000.939 ms |
| Stop to new Stopped heartbeat | 98.014 ms |
| Stop to Pre-operational request | 99.994 ms, after Stopped heartbeat |
| Stop to cleanup Disable Voltage | 349.101 ms |
| Stop to completed three fresh zero-speed SDO reads | 357.420 ms |
| Final fresh dual status | 0x14601460, both Switch On Disabled |
| Final NMT state | Pre-operational |
| Final independent/packed speeds and target | All zero |
| Heartbeat producer restored | 0 |
| CAN errors/drops | 0; kernel packet/byte counts reconcile exactly |

No post-Stop TPDO was observed or required. Final SDO zero proves recovered
zero speed and cleanup; **357.420 ms is not an NMT-only physical stopping-time
measurement**. Before Stop, right raw speed was 17..54 and left zero. The
operator accepted right-wheel motion then normal stop, stationary left wheel,
no restart or abnormal sound, stopped wheels and a safe site:
“全部符合，双轮已停止且现场安全”.

[Trial analysis](evidence/p6_nmt_stop_repair_20260911/physical_nmt_stop_once/analysis.json),
[preflight](evidence/p6_nmt_stop_repair_20260911/physical_nmt_stop_once/safety_preflight.json),
[operator record](evidence/p6_nmt_stop_repair_20260911/physical_nmt_stop_once/operator_observation.json)
and adjacent raw captures preserve the full chain. Executor and captures exited
successfully; JCAN configuration was unchanged. Do not reuse the one-shot marker.
Moving SIGTERM, remaining physical loss/fault/soak gates, applicability decisions
and final Phase 6 acceptance remain open.

A final process inventory found one unrelated passive candump that started
1196.2 seconds before this trial; it was left untouched. This trial's executor
and capture exited, and CAN counters were unchanged after cleanup. The initial
blanket no-candump assertion and the follow-up classification are preserved in
the physical bundle.
