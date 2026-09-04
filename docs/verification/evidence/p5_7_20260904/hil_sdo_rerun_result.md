# P5.7 final target HIL result

Date: 2026-09-04
Source revision: `0d32ca928e6c3969bd8a1007870a10e1b7e5cb27`

Result: PASS.

After the operator's successful 1200-frame periodic control, one separately
authorized node-1 read-only SDO upload was run with JCAN serial
`207F346D5650`. Before stimulus, the target harness proved these receivers were
active on `can0`:

- one unfiltered raw data socket;
- one all-error-frame socket;
- the normal observer's eight exact standard-frame filters, including `0x581`.

JCAN returned `ok=true` without warning:

- request: standard ID `0x601`, DLC 8, `40 08 20 00 00 00 00 00`;
- response: standard ID `0x581`, DLC 8, `4B 08 20 00 C8 00 00 00`;
- decoded unsigned value: 200.

The target raw capture preserved both frames 310 microseconds apart. The normal
observer published snapshot version 2 and preserved the response as
`sdo_rejected`, with raw ID `0x581`, DLC 8, and the exact payload. Rejection is
expected because the normal observer owns no commissioning request context and
must not accept unsolicited SDO results.

Target counters advanced from RXF=1207/RXMF=393 to RXF=1209/RXMF=395. Target
TX stayed zero, `can0` remained ERROR-ACTIVE at 500000 bit/s with zero errors,
and no CAN error frame was captured. Both bounded processes exited and no CAN
receiver remained. JCAN configuration was unchanged; active/recent periodic
task lists were empty with no cleanup error.

This closes the P5.7 positive target-RX gate and final Phase 5 acceptance. It
does not claim CiA402 motion, NMT transition, SDO download, persistent drive
configuration, fault behavior, brake behavior, bus-off behavior, or load/soak
acceptance.
