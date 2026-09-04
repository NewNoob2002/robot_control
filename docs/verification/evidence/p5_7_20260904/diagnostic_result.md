# P5.7 target receive diagnostic result

Date: 2026-09-04
Source revision: `0d32ca928e6c3969bd8a1007870a10e1b7e5cb27`

The first 30-second setup window expired before any CAN stimulus. A proposed
90-second observer window was rejected by the observer argument validator and
its remaining `candump` process was stopped; it also carried no CAN stimulus.

The active diagnostic used a final 60-second window. Before transmission, the
target harness proved that an unfiltered raw CAN socket and the normal
observer were active. `/proc/net/can/rcvlist_sff` listed the observer's eight
exact standard-frame filters, including `0x581`.

JCAN then performed the one authorized expedited SDO upload to node 1 object
`0x2008:00`:

- request: standard ID `0x601`, DLC 8, `40 08 20 00 00 00 00 00`
- response: standard ID `0x581`, DLC 8, `4B 08 20 00 C8 00 00 00`
- decoded unsigned value: 200
- warning: none

The target files show that the receive window ran from approximately
02:48:00.509 UTC through 02:49:01.516 UTC. The JCAN success evidence filename
is timestamped 02:49:28.369 UTC. That timestamp is after the target window,
but the JCAN evidence does not expose the physical transmit timestamp. The
experiment therefore cannot prove whether the two frames arrived during or
after the receive window.

The observer log remained at snapshot version 1 and the unfiltered `candump`
log remained empty. Target counters increased from RX=4 to RX=6 and stayed at
TX=0. SocketCAN cumulative statistics ended at RXF=6 and RXMF=0. `can0` stayed
UP, LOWER_UP, ERROR-ACTIVE at 500000 bit/s with zero error counters. No CAN
receiver or observer process remained. JCAN configuration matched the
preflight readback and its active/recent periodic lists were empty.

Result: the drive response and zero observer transmission pass, but the P5.7
positive target receive gate remains blocked. This run does not justify a
Rockchip CAN driver defect claim because stimulus timing was not captured
inside the target receive window. Any further physical transmission requires
new explicit authorization.
