# Powered-motion TPDO speed diagnosis — 2026-09-11

Disposition: raw drive feedback inconsistency is reproduced in retained evidence;
the internal drive cause is not yet established. No new hardware operation,
motion command, firmware edit or parameter write was made for this diagnosis.
The operator confirms both wheels physically rotated in the corresponding new
trials. This does not establish wheel direction or mechanical acceptance.

## Confirmed observations

- Both new trials use TPDO1 0x181, DLC8, status 0x6041:00/32 followed by
  packed velocity 0x606C:03/32; type255 and event timer100. The accepted
  application-driven manual trial used this same mapping, not the older
  speed-first mapping. Both new capture pairs match byte-for-byte.
- Left/right trials have 75/72 TPDO frames; exactly60 each arrive between the
  nonzero and zero target requests. Median interval is49.947ms. The status
  halves change with controlword/target operations; all four velocity bytes
  remain zero. The stream is neither absent nor an entirely frozen frame.
- The manual trial has1207 TPDO frames,402 with nonzero speed and388 speed
  changes. Its median interval is49.945ms. Small raw values including1,-5,-7
  are present. Thus a universal low-speed rounding threshold and a permanent
  byte-offset mistake do not explain the difference. Operating-condition-specific
  filtering cannot be ruled out.
- Correction to the initial summary: new single-axis trials do not upload speed
  during the3s target interval. The first nonzero independent SDO samples occur
  39.240ms (left raw22) and42.299ms (right raw41) after the first zero request.
  Other reported ranges also belong to cleanup. In cleanup,
  require_zero_velocity_feedback returns at the first nonzero subindex, so
  subindex3 is not read until independent speeds have become zero. Its five
  zero readbacks per trial do not prove the combined object stays zero in motion.
- Historical watchdog trial4 contains independent right speed raw52 at
  line97 of its raw capture, before cleanup; adjacent TPDO frames still contain
  zero speed. This supplies running-motion evidence missing from the new trials.
  It still lacks a contemporaneous subindex3 upload during nonzero feedback.

## Source and configuration differences

qualify_first_motion_cia402 in communication/canopen/qualification.cpp reads
0x200F=1, temporarily writes0, enters Operation Enabled, runs the target once,
then restores1 after zero/Shutdown. capture_manual_tpdo neither writes0x200F
nor enables either drive; it polls all three speed subindices once per sample
group. The historical preflight recorded0x200F=1, but the accepted manual run
itself did not read that object, so its value is not an in-run measurement.

The powered loop checks stream freshness, enabled status, and the other
channel staying zero. It does not require the commanded channel to report
nonzero velocity. Exit0 therefore proves the bounded command/cleanup path,
not that TPDO speed tracks motion. Keep physical feedback qualification open;
do not treat this exit status as full HIL acceptance.

The local vendor Quick Start object dictionary describes both independent
0x606C:01/02 and packed0x606C:03 as0.1r/min, with packed signed16-bit left/right
halves. It describes0x200F as command synchronization selection. It does not
document a rule making packed feedback zero in asynchronous command mode.
Thus a0x200F-dependent driver behavior is a hypothesis, not a documented fact.
Source: docs/ZLAC8015D V4系列  CANopen通信快速上手说明Version 1.00-20251111(1).pdf,
object dictionary entries0x200F and0x606C; matching entries also appear in the
local Version1.01 communication examples PDF.

## Narrowed possibilities and discriminating check

Leading candidates are drive-internal packed-object updating dependent on
command mode/enable state, or TPDO mapping/cache updates in those conditions.
Capture/host decoding cannot explain already-zero raw bytes seen by both
receivers. No drive firmware defect is proven without separating these paths.

The smallest useful next diagnostic must obtain a close-spaced group of
0x606C:01,02,03 uploads while independent velocity is nonzero, and compare
with surrounding TPDO frames under unchanged mapping. Retain individual
timestamps; sequential SDO reads are not simultaneous.

1. If01/02 vary but03 stays zero, isolate the drive combined-object path.
2. If03 varies but TPDO stays zero, isolate its PDO mapping/update path.
3. Compare hand rotation with drive disabled under separately bounded0x200F=1
   and0 conditions, restoring the baseline with readback, to isolate the mode
   factor before another powered trial. This is a proposed experiment, not
   an already prepared or authorized executable.

Any powered follow-up must retain the existing one-axis+5rpm/3s envelope,
zero preflight and cleanup, with a reviewed diagnostic sampling path in the
RK3588 application. Extra uploads must not overrun the absolute zero deadline.
JCAN remains silent; no automatic retry or persistent parameter write. Do not
raise speed, blindly reverse mapping, or substitute SDO values into TPDO data.

## Reproduction

Run `python3 docs/verification/evidence/p6_review_motion_20260911/analyze_tpdo.py`.
This reads four existing captures and writes tpdo_diagnosis.json, including
raw source line numbers, intervals, downloads and phase-labelled SDO samples.
The earlier postflight_verification.json files retain their whole-capture raw
ranges; this report supplies the necessary sampling-phase qualification.
