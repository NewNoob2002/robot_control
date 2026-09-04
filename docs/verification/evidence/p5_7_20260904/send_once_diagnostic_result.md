# P5.7 time-aligned send-once diagnostic

Date: 2026-09-04
Source revision: `0d32ca928e6c3969bd8a1007870a10e1b7e5cb27`

The user explicitly authorized exactly one non-periodic standard Classic CAN
frame through JCAN serial `207F346D5650`: ID `0x601`, DLC 8, payload
`40 08 20 00 00 00 00 00`. No retry, NMT, SDO download, reset, power cycle,
or parameter modification was permitted or performed.

Target baseline was `can0` UP, LOWER_UP, ERROR-ACTIVE at 500000 bit/s with
RX=6, TX=0 and zero error counters. JCAN configuration matched the prior
readback and its active/recent periodic lists were empty.

The target receive window ran from `03:12:48.384481355Z` through
`03:13:49.387603827Z`. At `03:12:49.390511633Z`, before transmission, the
harness proved that both receivers were active:

- one unfiltered `rx_all` raw socket on `can0`;
- eight exact `rx_sff` observer filters, including `0x581`.

JCAN `send_once` returned `ok=true`, no warning, with evidence timestamp
`03:13:08.411701Z`, inside the target receive window. At
`03:13:36.798071757Z`, while both sockets were still registered, target state
was RX=7, TX=0. The unfiltered socket match count remained zero and every exact
observer filter match count remained zero. The raw log was empty and the
observer exited normally at snapshot version 1.

Postflight remained RX=7, TX=0, RXF=7, RXMF=0, with zero CAN errors and no
receiver or observer process. JCAN configuration was unchanged and periodic
lists remained empty. Target driver inventory identifies `rockchip_canfd`
version 6.1.84 on platform device `fea60000.can`. The target configuration has
`CONFIG_CAN_RAW=y`, `CONFIG_CAN_RX_OFFLOAD=y`, `CONFIG_CAN_ROCKCHIP=y`, and
`CONFIG_CANFD_ROCKCHIP=y`. Matching headers and `/boot/config-6.1.84` are
installed, but the target source link contains no discoverable Rockchip CAN
driver source file, so source-level diagnosis requires the exact vendor BSP
tree used to build this kernel.

Result: FAIL for this specific `jcan_send_once`/`0x601` diagnostic. The known
single-frame stimulus was time-aligned with active raw and filtered sockets, the
target CAN receive counters advanced by one, but SocketCAN recorded no socket
match. It does not prove that a valid `0x601` data frame reached the wire or that
the drive returned an SDO response during this send-only operation.

A later operator control transmitted standard ID `0x7FF` every 50 ms and was
received normally by `candump`; cumulative statistics reached RXF=1207,
RXMF=393 and a 20 frames/s maximum. That control supersedes the initial generic
kernel/BSP delivery diagnosis. The remaining hypotheses are specific to JCAN
single-shot behavior, ID `0x601`, or an unmatched CAN error frame.
