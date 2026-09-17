# F1 zero-target X1 / quick-stop recovery A1 — FAILED / INCOMPLETE

F1 A1 ran once after fresh readiness and remains FAILED / INCOMPLETE, with both
markers consumed. Operator reports only CH6; no X1 operation. Only fault_ready
was reached, about5.160s before the60s control deadline; no fault_observed or
recovery phases occurred. Application exited1 with control_hil_recovery_incomplete.
The late phase prompt was received together with exit and was not forwarded as
an instruction. This trial does not establish an X1 recovery defect or acceptance.

Offline audit matches8912 frames across both captures,6002 zero RPDOs, zero speed
feedback, inactive X1, healthy mode/fault, complete29485-row trace, and exact36
volatile writes/restoration.605A:00=5 preceded all writes. CAN errors/drops and
extended error counters did not increase. Normal recovery oracle remains FAILED;
--incomplete verifies only this incomplete trial's protocol/restoration.
Operator separately confirms both wheels stationary, no abnormal sound and drive
OFF. No retry or F2/moving trial started. Review phase coordination and obtain
fresh readiness before any new one-shot; P10.3 remains OPEN.

Artifact SHA256:
2f97bdbafdb370e70ae47231609cf1ee698e56af2652f6e12e6c041970389058.
Stage: /home/cat/.cache/robot-control/staging/p103-faults-2f97bdba-x1-a1-20260917.
Arguments: --interface can0 --device /dev/serial/by-id/usb-1a86_USB_Single_Serial_586D017868-if00
--duration-ms 60000 --zero-x1-recovery. Host Debug/sanitizer43/43, cross/ELF and
target identity/hash/device-free smoke pass. See parent README, authorization.json,
safety-preflight.json and stage-smoke.log; preflight's PREPARING label is retained
as the exact staged preparation record, while this README records staging complete.

## Original one-shot sequence (consumed; not a run instruction)

Confirm unchanged wiring, raised/unloaded untouched wheels, on-site emergency stop,
neutral sticks, CH6 released, X1 released/reset and powered readiness:
“F1零目标X1恢复已上电就绪”. Record actual receipt time; never reuse right A1 readiness.
Existing X1/INPUT2 wiring and settings are unchanged.

1. CONTROL_READY: neutral, press/release CH6 once; remain neutral.
2. FAULT_READY: press/lock X1; hold until the next prompt.
3. RELEASE_FAULT: reset X1; hold throttle forward, press/release CH6 once.
   Wait for NEUTRAL_READY. Both final targets remain independently zero-only.
4. NEUTRAL_READY: return sticks to neutral, CH6 released; wait.
5. REARM_READY: neutral, press/release CH6 once; remain neutral through
   ZERO_REENABLED, RECOVERY_COMPLETE and application exit/cleanup.

No wheel should move. Any unexpected motion or abnormal sound requires immediate
emergency stop/OFF. Missing phases time out and stay failed; no automatic retry.
After exit, remain neutral, power OFF and confirm stationary wheels, no abnormal
sound, actual X1 actions and final OFF.

run-recovery.py starts silent JCAN then target candump before the sole RK3588
application. Maximum60s control,90s outer guard,12s cleanup grace and120s/20000frame
capture.605A:00 must read5 before any write. No fault reset, persistent write,
nonzero target or interface reconfiguration. Runner rejects missing fresh readiness
before any subprocess; current JCAN baseline matches the prior configuration.

Fetch physical-once as recovery-target even on failure, then run
python3 analyze-recovery.py. Check complete trace, both captures, temporary
writes/restoration, application result and separate operator statement. The new
wrapper captures post-run CAN counters even on an ordinary application failure
before reporting it. Any failure stays failed. Zero JCAN timestamps, if observed,
support byte/order comparison only, not independent timing.
