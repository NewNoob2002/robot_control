# Review-fix HIL disposition — 2026-09-10

**Incomplete overall.** Non-actuating RK3588 checks passed. Status-first manual
wheel feedback was accepted by the operator on 2026-09-11; zero-enable and powered
trials (rounds 3–5) have not run.
Historical accepted trials do not qualify the new code. The execution redesign
was completed on 2026-09-11. Two application-driven physical runs completed:
the first was stationary, and the second captured manual wheel feedback.
The operator confirmed physical feedback association, manual backward polarity
and acceptance of small other-half readings for this rig. Application diagnostics
remain open.

## Round 1

Four audited aarch64 diagnostic programs were staged under the existing
unprivileged /tmp/robot-control-qualifications/review-hil-f576d0f375fe directory.
No production service was replaced. Platform integration and namespace-local
managed vcan tests passed without a managed-test skip. Four prohibited CLI
commands returned 2. Passive observer deadline/SIGTERM returned 0/143; target TX
stayed 506. No actual drive frames arrived in this round.
See [results and limitations](evidence/p6_review_hil_20260910_round1/test_result.json).

## Round 2

The separately authorized 16-read inventory had 16 requests and 16 matching
responses in both JCAN output and independent RK3588 capture. Both status halves
were 0x1421; targets, speeds and fault value were zero; mode was 3. TPDO1 was
0x181, type 255, event timer 100, with status mapping 0x60410020 followed by
packed velocity 0x606C0320. This proves readback, not physical wheel mapping.
A preparation-only candump argument failure sent no requests and is retained.

Attempt 1 passed fresh preflight and received TPDO, but the GO handshake timed
out before manual sampling. All 18 sent frames, including reserved NMT
Pre-operational cleanup, were independently captured. No retry was automatic.

After explicit renewed authorization, attempt 2 moved the handshake before device
operations. It passed preflight and sampled for 18.53 seconds before aborting:

- Send 159 read 606C:01 using standard Classical CAN ID 0x601, DLC 8, payload
  40 6C 60 01 00 00 00 00. JCAN acknowledged USB acceptance, but independent
  capture contains neither this request nor its response. TPDO reception
  continued during the one-second wait.
- Of 160 USB-accepted sends, independent capture contains the other 159 exactly
  in order, and 157 matching SDO responses. Final 0x000 / 80 01 cleanup is present.
  NMT state acknowledgement and final SDO readback were not obtained after abort.
- All 472 TPDO frames contain 21 14 21 14 00 00 00 00. No nonzero speed was
  observed. Physical operator rotation/timing was not established. Chat/tool
  latency prevented reliable phase prompts; the next manual test needs an
  operator-visible local timer or equivalent local coordination.
- Target TX remained 506, CAN remained ERROR-ACTIVE, error counters and RX drops
  were zero. JCAN and independent capture exited 0 with empty stderr.

The Rust JCAN send path reports completion after its USB command reply, without
physical CAN delivery confirmation. This agrees with its documented
usb_command_accepted result. The observations do not establish where the frame
was lost, or establish a drive or robot-control qualification-code defect.
No automatic resend, enable, nonzero target, mapping write or persistent
parameter change followed the failure.

See [attempt 2 verification](evidence/p6_review_hil_20260910_round2/manual_attempt_2/verification.json),
its retained runner, raw JCAN output and RK3588 capture. The JCAN-driven sequence
is retired from the main application qualification flow; its delivery uncertainty
remains open. Do not advance to powered trials on these results.

## RK3588 application executor — 2026-09-11

Following the operator's routing correction, the existing C++ manual-tpdo path
owns SDO, NMT, feedback monitoring and bounded cleanup on RK3588. JCAN observes
silently and target candump supplies an independent second trace. The historical
Python/JCAN transmit sequence is not the application under test and will not be
reused for primary qualification.

The manual path now verifies and retains status-first mapping. It rejects wrong
mode, mapping, DLC, nonzero target/speed or enabled/faulted baseline; it does not
write mapping or motor commands. It temporarily sets producer heartbeat to 500ms
to verify fresh NMT transitions and restores 0 on cleanup. Final checks include
zero targets/speeds, fault value and non-enabled state. The application prints
monotonic elapsed time and wheel phase once per sample group for a live terminal:
0–10s stationary, 10–25s left, 25–35s stationary, 35–50s right, 50–60s stationary.
Chat timing is not an operator timing source.

Validation of these changes:

- The updated vcan checks failed against the old remapping implementation, then
  passed with the revised application. Host and ASan/UBSan suites passed 57/57
  each; managed vcan tests actually ran.
- LLVM 22.1.8 clang-tidy on the changed implementation and test translation units
  exited 0, with 0 errors and 41 reviewed advisories. The initial invocation with
  no selected checks is retained separately.
- Fixed GCC 11.4 aarch64 cross-build and both ELF audits passed against the locked
  target sysroot. Application SHA256 is
  74206353ffd3f0c8b93547f2b88350d9edcf50862839015e348b708896deded0.
- Both artifacts were staged at
  /tmp/robot-control-qualifications/review-rk3588-74206353ffd3. The board's isolated
  qualification vcan test exited 0 with prohibited=0 and failures=0, without skip.
  This staging/testing sent no physical CAN requests.

See [software and target results](evidence/p6_review_rk3588_executor_20260910/test_result.json)
and [prepared physical manifest](evidence/p6_review_rk3588_executor_20260910/hil_prepared/manifest.json).
The reused coordinator has no JCAN send operation; it validates observed frames
against the prepared allowlist. Authorization remains false until the new exact
application manifest and operator-visible timing are confirmed. The new manifest
allows at most 265 SDO uploads, two temporary heartbeat downloads and three NMT
frames (270 total); no automatic retries, controlword, target or mapping writes.

Before authorizing the physical run, place the following read-only log view in an
operator-visible terminal. It waits for the next executor log; it does not start
the application or send CAN frames. Follow its phase field rather than chat.

```sh
ssh -o BatchMode=yes -o ConnectTimeout=5 robot-dev \
  'tail -n +1 -F /tmp/robot-control-qualifications/review-rk3588-74206353ffd3/trial_1/executor.log'
```

The prepared remote wrapper, manifest and authorization=false file were copied
and hash-verified on 2026-09-11. Preparation sent no physical frames. The user
subsequently explicitly authorized and confirmed readiness for this exact run.

### First application-driven physical run

The application, coordinator, JCAN and candump exited 0 after the full 60-second
capture. Both receivers recorded exactly the same 1990 frames in the same order.
Target TX increased from 506 to 776: exactly 265 SDO uploads, two heartbeat
downloads and three NMT frames. All 267 SDO transactions had matching responses.
JCAN sent no physical frame; its configuration remained unchanged.

Fresh Pre-operational heartbeat and final readback confirm zero targets, zero
independent speeds, zero fault value, both status halves 0x1421, and producer
heartbeat restored to 0. Target CAN error/drop counters remained zero. This
verifies the application's request/response and cleanup flow for this run.

**This is not clean HIL acceptance or wheel-mapping qualification:**

- Every SDO speed response and all 1207 TPDO1 frames reported zero speed. The
  operator subsequently confirmed forgetting to rotate; this is a stationary
  collection rather than evidence of absent feedback during movement.
- Full executor output contains one CO_CANsend Permission denied message and
  138 CO_epoll_processLast CAN Epoll error messages. The coordinator forwarded
  only phase messages and missed these application diagnostics during the run.
  This supervision gap violates the intended stop-on-error policy; the raw
  exit0 result is retained but does not establish acceptance.
- Pinned upstream emits the latter diagnostic at LOG_DEBUG when epoll_new is
  unhandled. The owner's deadline branch can call processLast before processing
  a pending event, which is a candidate explanation requiring regression
  verification. The exact rejected startup frame is not identified by its log.

See [complete postflight verification](evidence/p6_review_rk3588_executor_20260910/hil_prepared/postflight_verification.json)
and the preserved executor/capture logs. No automatic retry or rounds 3–5
followed this run. Historical runners and authorizations must not be reused.

### Second application-driven collection

The operator explicitly authorized another collection after confirming the first
run was stationary. The same binary and 270-frame manifest were retained. At the
operator's request an additional read-only candump was permitted, and rotation
was coordinated as left, stop, right, stop while watching continuous TPDO. The
application's fixed phase labels were not used to assign physical wheel identity.

Source inspection confirmed both previously observed diagnostic strings are
emitted at LOG_DEBUG. The second wrapper retained and marked these two exact
known diagnostics (startup Permission denied only once before manual-ready);
all other error/warning text aborts. This is an explicit diagnostic collection
policy, not evidence that the underlying issues are fixed. Offline checks verify
new errors, warnings and repeated startup denials are rejected.

The second collection completed with both receivers recording the same 1990
frames in order. RK3588 TX increased 776 to 1046; all 267 SDO transactions matched.
JCAN sent no physical frame. Cleanup confirmed Pre-operational by heartbeat,
zero targets/speeds/fault, status 0x1421 on both halves and heartbeat producer 0.
CAN errors/drops remained zero; application and capture processes exited 0.

- First dominant movement burst: SDO subindex 1 and TPDO low 16 bits changed.
  Independent signed raw samples ranged from -631 to +10.
- Second dominant movement burst: SDO subindex 2 and TPDO high 16 bits changed.
  Independent signed raw samples ranged from -3 to +626.
- During the second burst, five TPDO low-half readings were -4, +1, +1, +2, +2.
  These are retained; vibration, coupled motion or another cause is not assumed.
  SDO reads and TPDO samples are asynchronous, so exact pointwise equality is
  not asserted. Values here are raw protocol values, not converted RPM.
- Feedback returned to zero. On 2026-09-11 the operator confirmed: left wheel,
  stop left, right wheel, stop right. Together with the traces this establishes
  left feedback = 606C:01 / low16 and right feedback = 606C:02 / high16 for this
  rig and status-first mapping. In a subsequent confirmation the operator stated
  both wheels turned clockwise when viewed from the chassis left side, equivalent
  to backward ground travel. The dominant measured feedback is negative on the
  left and positive on the right. This verifies manual-feedback polarity for
  this observation, not powered target-command channel mapping or polarity.
  The operator explicitly accepted the recorded minor other-half readings as
  normal. Chassis movement or encoder variation are operator-proposed
  explanations; the individual cause was not isolated by measurement.
- One startup Permission denied and 142 known epoll debug diagnostics remain
  recorded; no new error/warning aborted this collection. Powered trials remain
  unstarted while application diagnostics remain open. The manual feedback
  observation itself is operator-accepted, including those minor readings.

See [second collection verification](evidence/p6_review_rk3588_executor_20260910/hil_trial_2/postflight_verification.json)
for signed ranges, timings, exact minor samples, raw captures and cleanup proof.
