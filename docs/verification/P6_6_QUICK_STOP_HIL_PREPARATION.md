# P6.6 Quick Stop HIL preparation

Prepared and executed: 2026-09-10. Status: **QUICK STOP OPTION 5 HIL PASS**.

## Executed result

The operator authorized deployment and one trial, then confirmed the expected
physical observation. The sole right-channel +5 rpm interval lasted
2001.018 ms before one Quick Stop. RK3588 and persistent silent JCAN captured
the same 203 frames. The SDO acknowledgment arrived in 0.308 ms and both
status halves reported raw 0x1407 (Quick Stop Active) in 39.620 ms.

Right velocity first read zero in 207.276 ms and all velocity views first
read zero in 209.322 ms. Small nonzero feedback rebounds followed; cleanup
continued polling and reverified all three views zero at 442.289 ms.
NMT Pre-operational followed at 443.862 ms, its heartbeat at 604.060 ms,
and both temporary values were restored/read back by 612.278 ms. There
was no terminal-command retry, Shutdown, Disable Voltage or re-enable.

The operator confirmed the expected counter-clockwise right-wheel rotation
viewed from the chassis right side, stationary left wheel, both wheels
stopping and no abnormal brake action or sound. Target TX/RX advanced by
66/137 frames, exactly accounting for both captures. No CAN error, SDO abort,
EMCY, error/drop growth or JCAN warning occurred. Configuration and process
cleanup passed. The packed-velocity contradiction remains unresolved.

An earlier passive-capture preflight rejected incompatible candump -L/-e
options before GO: the executor never started and CAN TX/RX did not change.
That failure is retained in evidence/p6_6_20260910_quick_stop_trial_1/.
After a bounded passive check of the corrected -ta/-e invocation, the one
authorized physical trial ran once. Full evidence and replayable analysis:
evidence/p6_6_20260910_quick_stop_trial_1_capture_fixed/.

## Exact proposed slice

Use the existing Debug-only qualification executor on the identified RK3588
and ZLAC8015D V4 node 1, Classical CAN 500000 bit/s. Independent observer:
Rust JCAN serial `207F346D5650`, persistent JSONL silent capture. Reconfirm
identities, adapter configuration and the staged ELF checksum before use.

Reviewed ELF SHA-256:
`6193a8df071c92c766189d9871094aa1dcbfbb88e82e0c8e1acb5dfa7acd86ba`.
Local artifact: `out/staging/p66-quick-stop-6193a8df071c/robot-control-zlac-qualification`.
Proposed development destination:
`/tmp/robot-control-qualifications/p66-quick-stop-6193a8df071c/robot-control-zlac-qualification`.
Deployment checksum was verified before execution. GCC and ASan/UBSan each pass 48/48 tests, including
Quick Stop success, missing acknowledgment and option mismatch. Clean-source
RK3588 qualification build passes 69/69 with a validated ELF audit.

Proposed executor arguments:

```text
--interface can0 --quick-stop-once 2:5 --duration-ms 2000
```

Exactly one right-channel +5 rpm interval; left target remains zero. A single
0x6040:00=0x0002 follows the non-renewable two-second interval. Existing
0x605A:00 must read back 5 before any temporary setup or enable. It is never
written. This qualifies only the captured option 5 behavior.

All frames below are standard Classical CAN data frames. SDO frames have
DLC 8 and node-1 NMT frames have DLC 2. No CAN-FD, RTR, extended ID, broadcast,
RPDO, reset, periodic traffic or automatic retry is included.

| Operation | ID | Payload | Bound |
|---|---|---|---|
| Read Quick Stop option | 0x601 | 40 5A 60 00 00 00 00 00 | Once before setup; exactly 2-byte response value 5 |
| Heartbeat setup / restore | 0x601 | 2B 17 10 00 F4 01 00 00 / 2B 17 10 00 00 00 00 00 | Baseline 0, temporary 500, restore 0; verify each |
| Command application setup / restore | 0x601 | 2B 0F 20 00 00 00 00 00 / 2B 0F 20 00 01 00 00 00 | Baseline 1, temporary 0, restore 1; verify each |
| NMT Operational / Pre-operational | 0x000 | 01 01 / 80 01 | One entry and final cleanup |
| Velocity mode | 0x601 | 2F 60 60 00 03 00 00 00 | Fixed mode 3, verify display 3 |
| Zero-target enable sequence | 0x601 | 2B 40 60 00 06 00 00 00; 2B 40 60 00 07 00 00 00; 2B 40 60 00 0F 00 00 00 | One sequence; newer dual status required at each step |
| Right target +5 | 0x601 | 23 FF 60 02 05 00 00 00 | Exactly once, 2000 ms |
| Quick Stop | 0x601 | 2B 40 60 00 02 00 00 00 | Exactly one attempt |
| Zero targets | 0x601 | 23 FF 60 01 00 00 00 00 / 23 FF 60 02 00 00 00 00 | Startup and bounded cleanup/readback |

Fixed expedited uploads use `40 index-low index-high subindex 00 00 00 00`
on 0x601, with correlated 0x581 responses. The existing allowlist is
0x1017:00, 0x200F:00, 0x603F:00, 0x6040:00, 0x6041:00, 0x605A:00,
0x6060:00, 0x6061:00, 0x606C:01/02/03 and 0x60FF:01/02.
SDO timeout is 500 ms; transition and settle deadlines are 2000 ms. Settle
polling uses one shared deadline and 50 ms intervals, never a renewed budget.
The conservative whole-run authorization ceiling is 16 SDO downloads,
300 fixed uploads and two node-specific NMT frames, including failure cleanup.
The only nonzero target and Quick Stop attempt remain individually limited to
one each. A failed motion interval may attempt an extra zero before cleanup;
it cannot retry the nonzero target or terminal controlword.

The stop CLI has a one-second passive startup window followed by a fixed
read-only online probe if no boot/heartbeat arrived. The drive must already
be powered and the observers ready before execution; no 180-second power-on
window applies to this command.

## Pass and cleanup

Both captures must start before the executor. Bound each passive capture to
60000 ms / 10000 frames; use no independent active requester during the core
trial. Record first nonzero-to-Quick-Stop latency (accept at most 2020 ms),
acknowledgment, newer dual Quick Stop Active state, all independent/packed
velocity readbacks, NMT state and complete volatile restoration. A matching
masked state is required; the simulated raw 0x1407 is not a hardware fact.

After the terminal controlword attempt, cleanup must never send Shutdown,
Disable Voltage, another Quick Stop, or Enable Operation. It writes/verifies
zero targets, verifies velocity zero, enters and verifies Pre-operational,
and restores/uploads 0x200F=1 and 0x1017=0. Any failure remains a failed
trial even if cleanup succeeds. If CAN cleanup cannot complete, the operator
must remove power; no wire-level stop is then claimed.

Preflight requires both wheels raised and unloaded, clear area, released
mechanical brake, available operator and immediate independent power cut.
The operator must confirm only the right wheel moved, its direction, stopping,
and absence of abnormal brake action or sound. The existing packed-feedback
contradiction is retained; packed zero alone cannot prove no wheel motion.

The original proposed preflight remains preserved in
`evidence/p6_6_20260910_quick_stop_preparation/safety_preflight.yaml`,
The executed evidence contains the completed authorization and fixture fields.
The completed authorization covers only this trial; AGENTS.md requires fresh
dated authorization for every subsequent physical stimulus.
