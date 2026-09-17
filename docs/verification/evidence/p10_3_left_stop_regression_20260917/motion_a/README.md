# Held-input left-wheel A1 — executed, feedback criterion FAILED

User explicitly requested further motion validation after the R0 finding:
启动运动测试进行进一步验证. Latest explicit disposition is drive OFF, sticks
neutral. The preparation below preceded fresh powered readiness and the single execution
recorded in the outcome section. Both markers are now consumed; final OFF confirmed.

R0 natural release produced six right-negative candidates with no CAN send.
This A diagnostic excludes return-to-neutral during active motion: maintain
forward/right input through the application's automatic cutoff, then neutral
after STOP_VERIFIED or confirmed application exit. Any active wrong-wheel or
negative candidate still fails; it is neither masked nor tolerated. If input
changes before cutoff, the trace cannot be accepted as a held-input comparison.

Exact target: robot-dev, machine6923ab3301fb4a8d816759b04ec6bf0a, can0/node1
ZLAC8015D; original UART586D017868 and JCAN207F346D5650. Raised/unloaded wheels,
available emergency stop, no wheel contact. Unchanged left positive<=5rpm,
right zero, nonzero<3s, automatic cutoff near2950ms;60s operator wait. No B,
reverse/right-wheel trial, loaded use, reset, persistent write or network change.

Use artifact9ac673e9b7b039467e346ac6595347c0da5e03cc37a01b8e90b1773df43fa7f2
at /home/cat/.cache/robot-control/staging/p103-left-held-9ac673e9-a1-20260917.
The binary and59 compiled source/header hashes match the verified v3 snapshot.
Prior Debug42/42, sanitizer42/42 and cross/ELF evidence applies to this unchanged
binary. New wrapper syntax and rejection of missing powered confirmation are
checked; target identity/hash/help/pure-cycle smoke passes. JCAN self-test, scan
and config-get pass; configuration exactly matches the first left-motion baseline.
No current-tree remote CI result is claimed.

Fresh operator statement A工况已上电就绪 must confirm neutral sticks, CH6
released, raised untouched wheels and emergency stop. The orchestrator requires
a recent explicit confirmation before starting silent JCAN; it then starts target
candump before the actual RK3588 application. CONTROL_READY permits one CH6
press/release while neutral. MOTION_READY permits one forward/right push and HOLD.
STOP_VERIFIED permits return to neutral; it is zero-speed evidence, not full
trial acceptance. STOP_NOT_VERIFIED requires immediate emergency stop/power OFF.
Unexpected wheel motion, motion beyond3s or abnormal sound also requires OFF.

After exit, fetch both captures and full trace. Compare all frames, original
target/feedback envelope, exact volatile restoration, explicit stop cause2, raw
input held through cutoff, and stop waveform. The existing analyzer must retain
negative feedback as failure; separately inspect restoration even on failure.
An application exit0 alone is not acceptance. No automatic retry. Operator
wheel/direction/stop/noise and final OFF confirmation remain mandatory afterward.


## Executed A1 outcome

Fresh A工况已上电就绪 was registered before silent JCAN, target candump and the
single application invocation. App/target runner exit1; JCAN exit0. The app
correctly latched stopping feedback failure (not recorder overflow). Original
independent oracle also fails the positive selected-feedback envelope; its output
is retained in original-oracle.log. inspect-failure.py audits failure separately
without changing the criterion. Its initial local draft incorrectly expected a
606C:00 readback; corrected to the captured606C:01/02/03 subindices before producing
the independent restoration result. This was an offline audit error, not a new run.

Both captures match all6355 frames. Trace contains21178 complete records, no
overflow, feedback_bad1. Explicit stop cause2 identifies automatic cutoff.
Left targets3..5rpm last2950.300ms, right targets and feedback remain zero; no
nonzero target follows stop and no negative target is transmitted. Input ramps
forward/right and remains positive through cutoff; it is not perfectly constant.
In the final second CH1 is1512..1534, CH3 is1636..1666; final candidate/approved
command remains left5/right0. No pre-stop return-to-neutral is present.

After zero/Shutdown the five left-negative samples are -1.5rpm at62.531ms,
-1.2rpm at112.533ms, and -0.1rpm at262.574/312.587/362.602ms. Final zero feedback
begins412.630ms and spans200.042ms in candump; application stop verification is
614.899ms. Exact36 volatile writes, NMT sequence, final mappings/watchdogs/zero
readbacks and Disable Voltage baseline are independently verified. Restore ok1.
The wrapper exits on app failure before its routine can-after snapshot, so no
post-trial counter-delta verification is claimed. Raw waveform is stop-waveform.json.

Operator confirms left direction/stop normal, right always stationary, no abnormal
sound and drive power OFF (operator-post-trial.json). No retry or B started.
Conclusion: active stick return is not necessary to reproduce left post-stop
negative feedback. This does not identify drive control, mechanical motion or
velocity-estimation cause. Input/peak speed differ from the historical attempt;
do not claim a controlled quantitative reduction from -3.6 to -1.5rpm. The
original criterion remains FAILED; physical appearance and successful restoration
do not make it pass.
