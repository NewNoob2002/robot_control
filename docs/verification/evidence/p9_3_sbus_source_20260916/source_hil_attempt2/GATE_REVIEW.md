# Startup oracle correction — original report retained

analysis.initial.json and analyze_source.initial.py are the original result/oracle. They reported startup_held_disabled=false because they required every raw CH6 value from time0 to2s to be high. The other ten gates passed. No source code or recorded data was changed after this run.

The P9.0/P9.3 contract explicitly permits startup fragments/stale prefixes and requires inhibition during recovery, followed by no auto-enable while held. Attempt2 starts with four frames showing CH6=200 and nonneutral throttle, then CH6=1800 from0.003639621s. Two startup candidates were rejected. Every startup command was zero/invalid with authorization0. After the three-read recovery, CH6 stays high and health stays disabled throughout0.022259177–2.997298694s (and until the actual release at3.711283s). First authorization occurs only on the fresh press at7.134455076s.

The corrected v2 gate keeps all startup snapshots, requires zero/invalid/authorization0 for the entire0–3s interval, and requires every communication-qualified disabled snapshot to show held CH6, spanning at least1s. It does not drop the stale prefix or accept an enabled startup. A negative check changing the first inhibited left_rpm from0 to1 is rejected by the corrected oracle.

This changes an invalid raw-input precondition into the contract's publication/authorization condition. It is an oracle correction, not a retry, firmware fix, or retroactive relabeling of attempt1. Attempt1 remains FAILED with its original report and multiple unmet manual gates.

The four prefix frames have old-state characteristics; no specific USB/driver queue origin is asserted without lower-level evidence. Their presence remains a documented receive-time limitation, not proof of transmitter sample freshness.
