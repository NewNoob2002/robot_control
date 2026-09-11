# Application diagnostic correction — 2026-09-11

## Root causes and correction

The local client/observer inherited the upstream NMT slave INITIALIZING state.
CANopenNode attempts boot-up even with producer heartbeat zero; the strict TX
gate correctly rejected it and CO_CANsend logged Permission denied. Lifecycle
now establishes the local PRE_OPERATIONAL state after every successful init,
including reopen. This is a pinned-stack adaptation of public state fields:
the internal-command API processes commands after the boot-up attempt and cannot
prevent it. Remote NMT observations, heartbeat configuration, vendor sources and
both transmit gates are unchanged.

After epoll_wait returned a CAN event across the caller deadline, run_until
called processLast before handling that event, producing the unknown-event
message. It now clears the pending-event marker at that deadline boundary.
The level-triggered CAN socket retains the frame for the next call. Timer/event
fds are already handled by CO_epoll_wait; signal handling retains priority.

The managed-vcan regression counts the upstream diagnostics and deliberately
delays a real CAN-readable event across the deadline. Before the correction it
failed startup, reopen and epoll diagnostic assertions. After the correction it
passes and proves the frame is deferred, then observed exactly once. Existing
signal, receive-failure, link-loss and zero-transmit checks also pass. No
production log filter or successful-send simulation was added.

## Validation

- Host qualification: 57/57; ASan/UBSan: 57/57; managed vcan executed.
- Default Debug: 28/28; default Release: 28/28; commissioning isolation: 36/36.
- Scoped LLVM 22.1.8 clang-tidy: exit 0, zero errors, 16 advisories (header
  conventions, existing optional-access checks and linker-wrapper identifiers).
- Fresh locked-sysroot aarch64 Debug qualification app and both vcan test
  executables built; qualification app/test ELF audit passed.
- Target namespace-vcan results and staged checksums are retained in the evidence
  directory. These are simulated-bus tests on RK3588, not physical requalification.
- Full default RK3588 Debug/Release rebuild and remote CI for this final source
  remain part of P6.7 closure; prior-source evidence is not re-labelled.

Evidence: [diagnostic regression](evidence/p6_review_diagnostics_20260911/).
Historical HIL logs and their temporary diagnostic exceptions remain unchanged.
Any next physical runner must retain raw stderr and remove the previous two-message
exception: either diagnostic recurring makes the new session unclean.

## Remaining Phase 6 tests

| Test | Remaining acceptance |
| --- | --- |
| Fixed-artifact round 3: zero-target enable | Fresh baseline, each new matching state, no motion, bounded cleanup |
| Fixed-artifact rounds 4–5: one low-speed channel each | Powered command channel/polarity, fresh feedback, other wheel, zero/disable cleanup |
| Watchdog timing and recovery | Measured trigger/stop latency and terminal state; recovery cannot resume motion |
| Physical heartbeat loss | Detection, authority removal, zero response and safe recovery |
| Physical TPDO loss | Same, independently of heartbeat |
| SIGTERM during motion | Inhibit, bounded zero/safe state and process/resource cleanup |
| Controlled interface loss/reopen | Safe response and observation-only recovery |
| Applicable emergency/brake/fault-reset/electrical bus-off | Approved non-destructive stimulus, or individually documented residual/applicability decision |
| P6.7 closure | Final local/cross/target regressions, remote CI, evidence/provenance and residual acceptance |

The manual left–stop–right–stop feedback test is operator-accepted. Both wheels
were clockwise viewed from the chassis left side (backward travel); dominant
manual feedback was left negative/right positive. Small cross-half excursions
were accepted as normal; their physical cause was not measured. No repeat is
required just for this acceptance. Powered command mapping remains separate.
Historical NMT Stop, Shutdown, Disable Voltage and Quick Stop are accepted for
their recorded unloaded trials; they are not newly outstanding tests.

There are seven concrete remaining physical scenarios above (three review-fix
rounds and four loss/interruption scenarios), plus watchdog timing/recovery;
this is not a promise of eight attempts. Recovery can require separate trials,
and applicable fault stimuli add cases. Soak/real-time measurements do not have
a completed Phase 6 acceptance record and must be scoped for closure/production;
no arbitrary duration or additional pass is claimed here.
