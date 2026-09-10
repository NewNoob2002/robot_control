# P6.1 Evidence Preparation

This directory contains the 2026-09-04 software preparation and the first
authorized physical read attempt. Positions 1 through 96 completed with the
expected expedited widths. Position 97, `0x2000:00`, timed out waiting for
CAN ID `0x581`; the session stopped without retry and positions 98 through 119
were not sent.

Cleanup passed: the JCAN policy was restored byte-for-byte, its configuration
was unchanged, no periodic task remained, and RK3588 `can0` stayed
ERROR-ACTIVE with zero error counters and zero target TX. On 2026-09-04 the
operator explicitly accepted this command-session RK3588 transcript as the
independent capture for positions 1 through 96. The reported position-97
timeout remains failure evidence and was recovered only by the separately
authorized continuation.
