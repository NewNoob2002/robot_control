# P6.1 HIL attempt cleanup — 2026-09-04

The session stopped without retry when position 97 (SDO upload 0x2000:00) timed out waiting for CAN ID 0x581. Positions 98 through 119 were not transmitted.

- JCAN profile restored to the pre-test SHA-256: 752389399793ee8d50a25a8d30eecebc1879127d58e5f67bad79bcfc65ae13e1.
- JCAN raw configuration after the stop matches the preflight baseline exactly.
- JCAN periodic task list is empty and reports no cleanup error.
- RK3588 can0 remains UP, LOWER_UP, ERROR-ACTIVE at 500000 bit/s with TX/RX error counters zero and no bus errors.
- RK3588 cumulative RX packets changed from 9033 to 9226 (+193); cumulative TX packets remained zero. The delta is consistent with 96 request/response pairs plus the final request, but the final request line was not preserved in a durable candump file.
- A two-second postflight passive JCAN capture completed without warnings and observed no frames.
- No SDO download, NMT command, RPDO, reset, interface change, periodic transmission, parameter modification, or motion command ran.

Result: cleanup passed; the P6.1 evidence/test gate remains failed and incomplete. A retry or continuation requires a new explicit authorization and preflight.

