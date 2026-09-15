# Cable-loss software repair — 2026-09-14

The operator keeps the drive powered off. This repair has software evidence only;
the [physical cable trial](evidence/p6_cable_loss_20260914/RESULT.md) remains FAIL
with unverified protocol cleanup. No physical CAN, deployment, re-enable, power
cycle or retry was performed for this repair. All consumed runners remain consumed.

## Cause and changes

The physical application logged four CANopenLinux send errors, followed by owner
cancellation and zero-cleanup timeouts. The historical offline reproduction
identified 38 rejected timeout Abort attempts despite a passing vcan suite. The
pinned SDO client emits an Abort on its timeout, while the exact-request transmit
gate correctly rejects that unapproved frame with EACCES. This is not evidence
that sudo permissions need widening. Vendor sources and gate permissions remain
unchanged.

- Qualification uploads/downloads and commissioning uploads now enforce an
  absolute monotonic transaction deadline before asking upstream to process a
  timeout. They close locally and retain timeout failure and explicit retry rules.
- Every transfer records its transport/boot generation. A generation change
  cancels the transfer before an old response can be accepted as success.
- Shared local cancellation clears the receive flag and only the SDO client's
  unsent upstream buffer/count. Upstream Close alone only resets client state.
  Repeated cancellation is idempotent and preserves other pending buffers.
- External-loss qualification with a latched CAN error returns
  `qualification_external_bus_error` and `operator_power_cut_required`, inhibits
  the session and avoids generic cleanup writes on that invalid transport. It
  does not disable the communication watchdog or claim verified stopping.
- Lifecycle reopen checks administrative interface state before constructing
  another CAN socket. The existing post-open check remains for races. This
  removes repeated RX-buffer initialization logs while the interface is down.

The CLI returns failure directly; it does not run a second cleanup after this
external-loss result. Normal external loss without a CAN error retains the
existing bounded zero-first recovery path.

## Verification

Evidence: [repair logs](evidence/p6_cable_repair_20260914/).

| Check | Result |
| --- | --- |
| Added zero-send-diagnostics assertion against old code | RED, expected failure (`red_test.log`) |
| Initial repaired qualification vcan | PASS, no send diagnostics (`green_test.log`) |
| Full qualification host | 62/62 PASS (`host_tests.log`) |
| Full ASan/UBSan qualification | 62/62 PASS (`sanitizer_tests.log`) |
| Commissioning, including timeout/retry | 36/36 PASS (`commissioning_tests.log`) |
| Default Debug / Release | 28/28 each PASS |
| Added down/reopen regression, host and sanitizer | PASS (`reopen*_tests.log`) |
| Clean pinned RK3588 qualification / default Debug / Release and ELF audits | PASS (`final_cross.log`) |
| clang-tidy analyzer, bugprone, performance | No errors; 22 remaining diagnostics in qualification/shared error accessors; new lifecycle advisory corrected and rechecked |
| Target smoke / electrical cable, bus-off, power-loss and soak | NOT RUN |

Regression coverage includes timeout/no extra Abort attempts, preserving unrelated
pending buffers, idempotent close, rejecting an acknowledgement across remote
boot, CAN controller error with no cleanup target writes/watchdog restoration,
and repeated down-interface reopen without socket initialization. The CAN error
is a raw error frame injected only into isolated vcan; it is not an electrical
bus-off or physical watchdog qualification.

Builds use `cmake --build out/build/<variant> --parallel 4`; tests use
`ctest --test-dir out/build/<variant> --output-on-failure`. Variants are
`review-fixes`, `review-fixes-sanitizer`, `review-commissioning`,
`review-default-debug`, and `review-default-release`. The final cross source
snapshot is attested by `final-source-attestation.json`; the cross runner uses
the pinned offline container and the recorded RK3588 sysroot.

## Remaining boundaries

Local cancellation cannot retract a frame already submitted to the Linux kernel
or CAN controller. This change does not prove queue draining, final drive state,
or safe electrical recovery after reconnect. The observed TX warning/passive
errors remain real physical evidence; bus-off was not observed in that trial.
The repaired artifact needs separately authorized target and physical
requalification before further motion. Keep the drive powered off for now.

Future HIL capture handling must retain CAN error frames and tolerate candump's
indented error descriptions. The failed-run audit already preserves both raw
errors; old consumed coordinators are not reused. Power-loss, applicable fault
injection, brake applicability and soak gates remain open. An emergency-stop
input is connected according to the operator; an approved fault-injection method
is not yet available. Phase 6 acceptance remains open.
