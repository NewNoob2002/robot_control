# Dual-wheel transmitter-off A1 — INCOMPLETE (2026-09-20)

The 60-second preparation window expired with zero UART bytes/frames across
6002 read batches. Source remained revoked; no enable or movement occurred.
All 6003 RPDO targets were zero and both measured wheel speeds remained zero.
8989 frames match between target candump and JCAN; 36 volatile writes and full
baseline restoration are verified, with unchanged CAN counters.

The operator identified the cause: the SBUS data line was not reconnected after
the preceding UART test. This corrects the preceding UART A2 terminal OFF
response's reconnection claim only; its raw loss/motion evidence is preserved.
The operator explicitly authorized a new RF A2 after reconnection.

The supervisor displayed only WAIT because it did not relay the application's
waiting_link status. Source fault14 is the create-time shutdown/revocation
retained without fresh input, not proof of a new shutdown or RF loss. The
new A2 supervisor relays input status; this consumed A1 remains unchanged.

No transmitter-off motion acceptance, retry permit or standalone physical OFF
confirmation is inferred from this attempt. Terminal feedback, if supplied,
must describe the actual no-motion outcome. New A2 requires fresh local START.

Offline reproduction:

    python3 -B docs/verification/evidence/g3_remaining_motion_20260918/dual_rf_a1/analyze-no-input.py
