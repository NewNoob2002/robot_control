# Recovery observer and causal oracle correction

Native X1 recovery may reach Switch On Disabled without QuickStopActive. The observer
now recognizes this path, while an observed quick-stop still requires subsequent
Disabled feedback. The first new source authorization is pinned; a subsequent source
disable or generation change fails the attempt rather than borrowing another CH6 cycle.
The independent oracle binds transitions to the first post-rearm enable, checks one
continuous source authorization, and requires the announcement within100ms of feedback.
Both old quick-stop and native-disabled paths remain tested. No runtime safety policy,
command envelope, ±1rpm tolerance, or historic trial criteria changed.

Validation:
- Test-first native-X1 virtual reproduction failed with the old executable:
  control_hil_recovery_incomplete after generation2 already enabled (11.55s).
- Corrected recovery managed-vcan suite passed (60.45s); native X1 case added.
- Full Debug43/43 and ASan/UBSan43/43, no skips; see XML/logs.
- Scoped clang-tidy passed; locked offline aarch64 cross and ELF audit passed.
- Current compiled project inputs match the frozen cross source; see JSON manifest.
- A4 offline observed-sequences audit passes, while corrected combined oracle rejects
  the old mixed-event completion. Original report/analyzer retained in A4.

Cross artifact SHA256:
616e8ad3ec59c14985106c846c8f706b396617dc16b226b3012e3509fd821cea

No target staging, smoke or HIL performed for this artifact. Both old A4 one-shot
markers remain consumed. New hardware validation requires a separately prepared run.
P10.3 remains OPEN; moving fault/failsafe/SIGTERM tests are not signed off here.
