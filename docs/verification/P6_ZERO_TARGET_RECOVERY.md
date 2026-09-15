# Zero-target recovery after Quick Stop — 2026-09-11

Host and sanitizer verification pass after correcting an existing success-test
timing budget; failed diagnostic runs remain recorded below. The authorized
physical zero-recovery trial passes protocol, cleanup and operator acceptance.
The subsequently authorized NMT Stop trial failed during stop/recovery. The
operator confirms both wheels stopped and the site is safe; drive-state cleanup
remains unverified.

## Defect and repair

The preceding Quick Stop trial intentionally ended in Quick Stop Active.
The next NMT Stop trial's startup sent Shutdown and waited for Ready to Switch
On. The drive acknowledged the request but remained at raw dual status
07140714. Cleanup repeated that unsupported transition and also timed out.
Both captures agree: no nonzero target or NMT Stop command was sent, and every
velocity sample was zero. The operator confirmed no movement and a safe site.
The original failure remains in
[the trial record](evidence/p6_sync_remainder_20260911/nmt_stop/failure_analysis.json).

The vendor CANopen examples manual, Version 1.01-20260226, printed pages 8–9
(PDF pages 9–10), shows transition 12 from Quick Stop Active to Switch On
Disabled and assigns it to Disable Voltage. Shutdown covers transitions 2, 6
and 8, not transition 12. This is a documented route, not a physical pass on
this fixture. The illustrative Enable Operation exit is not used for recovery.

The private recover_quick_stop_at_zero helper is used by both the shared
zero-target activation sequence and ordinary zero-target cleanup:

1. Callers first write and read back both zero targets.
2. An observed Quick Stop in either status half selects recovery. Other initial
   states retain the existing sequence. Stale Quick Stop feedback fails the
   freshness gate; unknown or missing feedback is not treated as proof of recovery.
3. Require current heartbeat/TPDO, unchanged generation and clean observations;
   reject a fault in the other half and any nonzero packed velocity.
4. Read independent and packed zero velocities, then recheck current TPDO and
   zero packed velocity at the action boundary.
5. Send Disable Voltage (6040:00 = 0000) once. Require a newer single TPDO with
   both halves Switch On Disabled and zero velocity under the transition timeout.
   Recheck independent and packed zero speeds before returning.
6. Only the already-authorized activation caller may continue the existing
   Shutdown, Switch On, Enable Operation sequence. Cleanup never enables.

Recovery is attempted at most once per session. After a failed attempt, cleanup
still clears targets and attempts Pre-operational but cannot repeat Disable
Voltage; it reports qualification_quick_stop_recovery_consumed. This preserves
the failure and avoids turning cleanup into an automatic recovery retry.

The helper does not reset faults, retry, expand the transmit gate, add a CLI
activation API, or change permanent parameters. Successful explicit stop tests
retain their selected terminal state: terminal_controlword_submitted still
prevents ordinary cleanup from overriding Quick Stop/Disable Voltage.

## Verification

The existing managed-vcan zero-sequence fixture now includes two successful
recovery cases (online startup and cleanup), plus eight failure cases at each
boundary: nonzero independent speed; SDO abort; missing post-command TPDO;
opposite mismatched dual states in successive frames; moving disabled-state
TPDO; boot/generation change; nonzero post-transition independent speed; and
new moving TPDO arriving during otherwise-zero SDO reads. Exact peer exchanges
reject any unexpected enable or motion frame.

Two additional first-motion startup-failure cases prove that SDO rejection and
missing disabled-state feedback cannot cause cleanup to retry recovery. These
failed before the attempt latch was added; red_cleanup_retry.log preserves them.
The first success cases fail against the old implementation; red_vcan.log
retains that exchange history. No physical test was repeated.

| Check | Result |
| --- | --- |
| Final host qualification, including managed vcan | 60/60, no skips |
| Final ASan/UBSan suite, including managed vcan | 60/60, no skips; earlier failures retained and diagnosed below |
| Default host Debug / Release | 28/28 each |
| P5.6 commissioning regression | 36/36 |
| Fresh pinned GCC 11.4 cross builds | Qualification Debug and default Debug/Release pass |
| ELF audits against the locked target Ubuntu 22.04 sysroot | Pass |
| LLVM 22.1.8 scoped analysis | Exit 0; 44 exported warnings, no errors: 42 prior advisories plus two intentional flushes in failure-only diagnostics; none in the recovery helper |
| RK3588 isolated-vcan qualification | 12318 frames; prohibited=0, failures=0 |

Builds used a fresh immutable snapshot of the working source and the existing
locked container/sysroot. These are verification builds, not production release
publication. The temporary staged directory is
/tmp/robot-control-qualifications/zero-recovery-31d2d0848e93. Executable SHA256:
31d2d0848e9330f6f910e642d47bccb6552ca112cefa36214defa646c524b7e3.
Only the namespace-isolated test program ran on RK3588; the staged qualification
motion executable did not run. See the
[manifest](evidence/p6_zero_recovery_20260911/manifest_budget.json),
[test logs](evidence/p6_zero_recovery_20260911/) and
[source attestation](evidence/p6_zero_recovery_20260911/budget-source-attestation.json).

The earlier f299615e750c build and its successful software/target-vcan records
precede the no-repeat latch and are superseded. They remain preserved, as do
the initial red tests. The 7dcb656a56d2 build contains the final production
recovery logic but predates the final test-fixture correction and diagnostics.
Final host/sanitizer logs use final_; final cross/target records use budget.

### Existing success-fixture deadline defect

The once_ sanitizer suite failed 59/60; its focused recheck also failed.
sanitizer_diagnostic.log captures the first mismatch: Pre-operational was sent
where the peer expected the third zero-speed upload, and the application
returned qualification_sdo_deadline. The first-motion fixture deliberately
returns a nonzero independent speed, causing wait_zero_velocity to wait 50 ms
before repeating all three SDO reads. Its 60 ms transition budget left less than
10 ms for those exchanges and scheduling. Subsequent peer mismatches were a
consequence of that early inhibit, not proof of an RPDO mapping defect.

A bounded 15 ms delayed response reproduces qualification_zero_velocity_timeout
with the original 60 ms budget (red_zero_wait_budget.log). The success fixture
now retains that delay and uses 200 ms to accommodate polling and readback.
green_zero_wait_budget.log and final_sanitizer_ctest.log pass; production
deadlines and dedicated timeout rejection cases are unchanged. No sanitizer
memory-error or undefined-behavior diagnostic was observed in the failed runs.
Frame mismatch diagnostics are retained to make future exchange failures useful.

## Physical zero-recovery trial

After the operator's new authorization, the 31d2d0848e93 artifact ran
--zero-sequence once on the same RK3588/node1, with raised-wheel/emergency-stop
conditions carried forward from the operator confirmations. JCAN serial
207F346D5650 observed silently alongside target candump. The first staging
approval timed out before process creation; its permitted single retry
succeeded. The physical executor ran once without retry.

Both captures contain the same 122 frames and 52 completed SDO transactions.
Initial dual raw status 07140714 changed to 40144014 within 26.107 ms after
the single Disable Voltage request. Subsequent zero-only Shutdown, Switch On,
Enable Operation and cleanup Shutdown transitions were observed. Every target,
SDO speed and TPDO speed was zero. Cleanup finished at dual raw 21142114 and
Pre-operational, restored heartbeat producer to zero, and closed the executor
and both captures. CAN errors/drops were zero and kernel counters matched.
See [physical analysis](evidence/p6_zero_recovery_20260911/physical_zero_once/analysis.json)
and its adjacent raw captures, exact authorization and one-shot marker.
The operator confirmed both wheels remained stationary, with no abnormal sound
or restart. This zero-recovery trial is accepted.

## Historical hardware gate before the NMT feedback repair

The zero-recovery trial has protocol, cleanup and operator acceptance. The
subsequent newly authorized NMT Stop trial ran once and failed as recorded below.
Both the historical and new NMT one-shot runners are consumed; do not reuse them.
Repair and software-verify the NMT stop feedback/recovery path before preparing
any new hardware trial and its authorization. Earlier six passes and the original
NMT startup failure remain attached to their exact artifacts. Phase 6 and P6.7
final acceptance remain open.

## Subsequent NMT Stop failure — hardware work stopped

The new artifact ran one authorized right +5 rpm / 1000 ms NMT Stop trial after
zero-recovery operator acceptance. Both captures contain the same 147 frames;
CAN errors/drops are zero. One nonzero packed target and one NMT Stop were sent,
with 1000.889 ms between them. The executor reported
qualification_nonzero_tpdo_velocity, then cleanup qualification_dual_state_timeout.
Pre-operational followed NMT Stop after only 0.149 ms; no Stopped heartbeat was
observed. Thus this run does not prove the intended NMT Stop behavior.

Zero target and restored heartbeat producer were read back, and executor/capture
processes exited. However, no post-stop TPDO established fresh zero speed or
successful drive-state cleanup. The last TPDO was 2714274400003400, sampled before
NMT Stop. Do not infer physical standstill from target readback or process exit.
No retry was performed. The operator reported: “右轮逆时针旋转了一点然后停止了，
两轮已停止、现场安全”. This confirms observed standstill and site safety, not
protocol acceptance or verified drive-state cleanup. The observation viewpoint
was unspecified; do not derive a new axis-sign convention from “counterclockwise”.
See [failure analysis](evidence/p6_zero_recovery_20260911/physical_nmt_stop_once/failure_analysis.json).

## Subsequent repair acceptance

The newly authorized [NMT Stop feedback and cleanup repair](P6_NMT_STOP_REPAIR.md)
passes one physical trial with 140 matching frames, a fresh Stopped heartbeat,
verified Pre-operational SDO cleanup and operator acceptance. The failures
above remain failures of their exact historical artifacts. No consumed runner
may be reused.
