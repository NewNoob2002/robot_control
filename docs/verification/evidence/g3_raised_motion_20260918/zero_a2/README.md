# G3 new-artifact zero-target A2 — PASS

2026-09-18. Artifact5f44146c71747eb93cdf30c8ae675b3c9fb48566c7951c53daa96dbed627c14a
on the identified RK3588, node1/can0, UART586D017868; JCAN207F346D5650
normal receive/hardware ACK, no data-frame sends. The operator explicitly
confirmed actual drive power and authorized A2 once in the local START window.
The separate A1 first-upload timeout remains failed/incomplete: operator clarified
that drive power was OFF. No source or binary change was made for A2.

## Independent evidence

-20-second zero-only control window; process exit0 after21.621s including setup
 and cleanup.2001 cycles,1712 enabled samples,2857 SBUS frames, no discontinuity.
 Fresh neutral/released CH6 followed by the operator's new CH6 edge is present.
 All candidate/selected/approved wheel targets and2002 RPDO targets are zero.
-3230 dual-capture frames match globally in order and payload, without any
 G2 capture-order exception. All2163 application TX frames match wire capture.
-411 dual-axis feedback frames stay within the approved+/-2rpm band;
 observed raw minimum/maximum are-0.2/+0.2rpm, retained without alteration.
 Startup and cleanup standstill holds are162.022ms and162.003ms.
-Exactly36 allowed SDO downloads, three expected NMT messages, complete
 baseline readback and restoration, zero targets and final Disable Voltage
 independently verified. CAN error/drop/controller counters are unchanged.
-JCAN supervisor intentionally stops after target completion: its generic
 INTERRUPTED label means requested STOP here; error=null, shutdown ACK and
 process exit0 are recorded, with matching captures. No USB reconnect occurred.
-Local OFF confirms both wheels stationary throughout, no abnormal sound and
 drive power OFF. Host/capture and target one-shot markers are consumed.

Three missed periods and maximum lateness35.901ms remain in the raw summary;
maximum cycle execution696us and internal shutdown36us are observations,
not hard-real-time guarantees. This short prerequisite does not qualify motion,
loaded/ground operation, endurance or the delayed-TX kernel issue.

## Reproduction and files

From the repository root, the retained independent zero auditor is runnable:

    python3 -B docs/verification/evidence/g3_raised_motion_20260918/zero_a2/analyze-zero.py
    python3 -B scripts/test/analyze_control_hil_trace.py docs/verification/evidence/g3_raised_motion_20260918/zero_a2/physical-once/application.log --zero-feedback-tenths-rpm 20

analysis.json includes trace/wire identity, exact-zero decisions, fresh CH6,
standstill holds, unchanged CAN counters and operator disposition. zero-analysis.json
is the protocol/restoration auditor output. Raw target data are both extracted
under physical-once/ and retained in target-result.tar.gz; JCAN data live under
jcan/. No consumed runner may be executed again.

The next signed-motion item is left-negative; manual coordination, a newly
reviewed one-shot trial and fresh powered readiness/authorization remain required.
