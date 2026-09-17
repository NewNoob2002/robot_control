# Explicit single-throttle right-wheel qualification mode

--right-throttle enables a startup-only option on the existing Debug-only ControlLoop.
It requires neutral steering and nonnegative throttle; incorrect input stops before
submitting a new command. Only the qualification copy of SBUS left command is zeroed
before arbitration. Raw Source/candidate, timestamps, sequences, validity, CH6 authority
and all fault/inhibition paths are preserved. Default differential modes are unchanged.
The independent RPDO gate remains right<=5rpm/<3s, left0, no restart; strict motion
feedback remains unchanged. No production Source policy, calibration or drive settings
were modified. Existing trace shows raw candidate and selected/approved command separately.

Test-first old CLI rejected the new mode. Six new virtual cases pass: throttle-only,
X1, steering rejection, reverse rejection, failsafe and silence. Full Debug43/43 and
ASan/UBSan43/43 pass without skips, including preservation of raw bilateral candidates
and right-only approved commands. Static checks, locked offline cross and ELF audit
pass. Compiled inputs match frozen source. Target help/hash/pure cycle smoke passed.

Artifact778b0a79b4985b1040f2eccee1fbe6e948c8084a61e3c645db7a2a537d7ac611.
Physical F3 A2 confirms single-throttle right motion, but X1 arrived after automatic
cutoff and post-stop negative feedback remains failed. No F3 acceptance is claimed.
