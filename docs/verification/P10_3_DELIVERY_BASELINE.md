# P10.3 source and evidence delivery — 2026-09-17

Bounded unloaded HIL is accepted in [the checkpoint](P10_3_HIL_CHECKPOINT.md).
This record separates physical acceptance from source delivery and remote CI.
Base commit4c7390f on codex/phase9-sbus-development; no production behavior change
was made during this delivery review. Final commit and CI disposition follow in
the delivery evidence after push; local success is not remote-CI success.

## Reviewed scope

- Single-owner ControlLoop diagnostic batch copy and opt-in right-throttle
  qualification projection; invalid/reverse/steering input retains inhibition.
- Independent positive single-wheel send gate, first-zero latch,3s default/8s
  explicit bound; SDO targets remain zero-only. Bounded trace preserves raw values,
  hard faults and separately pending negative stop-band reviews.
- Exact baseline ownership and restoration, explicit0..20-tenths standstill
  tolerance (library default0), X1/SBUS/UART recovery observers, neutral fresh
  authorization and first-rearm causal checks. No automatic reset or motion rearm.
- Failure/cleanup, signal ordering, generation invalidation, borrowed lifetimes,
  signed feedback decoding and boundary tests reviewed. No new blocking semantic
  defect identified within this scope; this is not kernel or production approval.

The accepted91d3ce99c06b4f96148a1e8cc86412459d53fe1bce88a91ec41f1d64dc33430c
artifact matches all109 compiled project-input hashes. Cross/static/ELF evidence
is reused from uart_stable_preparation because those inputs are unchanged.
Existing full Debug/sanitizer46/46 and F6 focused two-case tests in each variant
remain applicable. Final owner/guard regressions and remote CI are recorded
separately below/in the delivery evidence. Local sanitizer uses detect_leaks=0;
remote CI uses its configured leak checking. No new HIL is run for documentation
or CI-only edits, and no changed binary is claimed physically qualified.

## Evidence preservation and reproducibility

[Delivery inventory](evidence/p10_3_delivery_20260917/evidence-manifest.json) hashes
all1473 existing P10.3 evidence files, including earlier failures and original
consumed runners. python3 scripts/test/test_control_hil_evidence.py checks this
inventory,1941 trial checksums,255 archived Python scripts and F6 OFF/consumption.
It never executes a trial runner, uploads an artifact or accesses a device.

The fault/recovery top-level README gained a latest-status banner after F6.
Only its entry in that directory's SHA256SUMS was stale; the old checksum list is
preserved verbatim as SHA256SUMS.before-f6-index and only the README digest is
updated. No original raw capture, application result or trial script was changed.

CI now requires the window/startup/UART vcan test names in addition to existing
motion/recovery tests and runs the evidence check in the debug runtime job.
The unrelated untracked local .clang-tidy is excluded from this delivery and
left untouched. Source and accepted test/evidence changes form one integrated
P10.3 commit: splitting historical physical evidence away from its test dependencies
would leave intermediate commits unable to reproduce the full acceptance suite.

## Remaining boundaries

F4 A6 stop-feedback review items remain pending; their failure is not rewritten.
F5 proves Reader raw-byte silence with partial-timeout withdrawal, not measured
voltage silence. F6 signal timing is target monotonic send observation, not an
independent JCAN latency measurement. All physical runners remain consumed.
P6 closure/soak, matching kernel retry-worker repair, negative/dual-wheel/loaded
operation and production service/ROS2 work remain outside this acceptance.

## Final local checks

Both existing HIL builds report no work to do. Focused control-cycle/runtime-policy/
trace/zero-gate/motion-gate tests pass5/5 Debug and5/5 ASan/UBSan without skips.
The separate runtime build was rebuilt and control_loop_vcan_managed passes1/1
in an isolated network namespace. XML is in p10_3_delivery_20260917.
P6 archive/link checks, P10.3 inventory/syntax checks, CI routing, actionlint and
git diff --check pass. No raw evidence secrets/private-key markers or individual
new files exceeding20MiB were found. This review did not contact powered hardware.
