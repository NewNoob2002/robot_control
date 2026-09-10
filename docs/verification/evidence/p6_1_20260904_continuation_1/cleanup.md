# P6.1 continuation-1 cleanup — 2026-09-04

All 23 authorized uploads passed. The explicitly authorized retry of 0x2000:00 returned 00 00; only then were positions 98 through 119 sent once each.

- RK3588 raw capture contains exactly 46 frames: 23 requests on 0x601 and 23 matching responses on 0x581.
- RK3588 cumulative RX packets changed from 9226 to 9272 (+46); cumulative TX packets remained zero.
- can0 remained UP, LOWER_UP, ERROR-ACTIVE at 500000 bit/s with TX/RX error counters zero and no bus errors.
- JCAN raw configuration after the session matches the baseline exactly.
- JCAN periodic task list is empty and reports no cleanup error.
- The original JCAN policy was restored to SHA-256 752389399793ee8d50a25a8d30eecebc1879127d58e5f67bad79bcfc65ae13e1.
- No candump process remained. A two-second passive JCAN postflight capture completed without warnings and observed no frames.
- No SDO download, NMT, RPDO, reset, interface change, periodic transmission, parameter modification, or motion command ran.

Result: continuation-1 and cleanup passed. The complete 119-object JCAN inventory now exists; positions 1 through 96 still lack a durable independent RK3588 raw file from the initial attempt.

