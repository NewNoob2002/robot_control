# F1 zero-X1 A4 — executed; two recovery events distinguished

Original application and oracle returned PASS, but the original combined causal
claim is superseded. Original recovery-analysis.json and original-trace-analyzer.py
are preserved. Both one-shot markers are consumed. Operator entered OFF:
wheels stationary, no abnormal sound, drive power OFF.

Operator confirms first neutral CH6 rearm, waiting at a stalled prompt, then another
CH6 off/on. Independent audit-observed-sequences.py verifies two separate intervals:

- Source generation2: X1 recovery through native Disabled (0x1460), then fresh
  Shutdown/Ready/SwitchOn/Enable and at least one second enabled at zero target.
- Source generation3: later CH6 quick-stop, explicit Disable Voltage, fresh Disabled,
  Shutdown/Ready/SwitchOn/Enable and at least one second enabled at zero target.

The old observer required QuickStopActive even for native X1 recovery, so it missed
first recovery and borrowed the later CH6 event. The corrected shared oracle rejects
A4 as one clean recovery scenario; observed-sequences-analysis.json preserves the
narrow retrospective findings. No new physical run or corrected-binary HIL is claimed.

6159 target/JCAN frames match; 4036 RPDOs all have zero targets; exact36 volatile
writes restore the baseline. Complete19989-record trace, no overflow/feedback_bad
or discontinuities. A4 feedback was exactly zero on both axes, despite configured
±1rpm tolerance. CAN error/drop counters unchanged. Original artifact:
4c7a757f578f774c0788221bd5d46da3247f53e9ca898001ef41b7f15472256f.
A1/A2/A3 remain failed. No motion/failsafe/SIGTERM acceptance follows from this audit.
