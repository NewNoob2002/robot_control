# P6.6 NMT Stop HIL Preparation

Prepared: 2026-09-08T08:17:07Z; startup-online correction reviewed 2026-09-08T09:40:55Z.
Status: **NMT STOP HIL PASS — FIRST P6.6 PHYSICAL SLICE COMPLETE**.

## Scope

The first P6.6 slice tests one NMT Stop during the already mapped right-wheel
command. The Debug-only qualification owner accepts:

```text
--nmt-stop-once 2:5 --duration-ms 2000
```

It first prefers passive boot-up or heartbeat evidence for one second. If the
process starts after the drive and sees neither, it performs one allowlisted
read-only upload of `0x1017:00`. An exact response proves node-1 SDO liveness;
the same owner then changes volatile `0x1017:00` from 0 to 500 ms and requires
a new heartbeat before any motion preflight. It then verifies zero targets and
zero feedback, changes volatile `0x200F:00` from 1 to 0, enters velocity-mode
Operation Enabled, writes only
`0x60FF:02=+5 rpm`, and sends node-1 NMT Stopped after 2000 ms. After observing
`0x701#04`, the same owner enters Pre-operational, writes and verifies both
targets zero, briefly returns to Operational with zero targets for verified
Shutdown cleanup, enters Pre-operational, and restores `0x200F:00=1`.

The trial does not expose Quick Stop, `0x2000`, `0x605A`, fault reset, node
reset, broadcast NMT, reverse motion, a left-wheel nonzero target, brake-output
writes, EEPROM writes, or retries.

## Safety review correction

Review found that the initial recovery path sent NMT Operational even when the
expected NMT Stopped heartbeat timed out. That could re-enter Operational after
an unverified stop. The shared recovery now sends Pre-operational, verifies both
targets zero, and returns the timeout without sending Operational. A managed-vcan
regression withholds `0x701#04` and proves this exact fail-closed sequence.

If NMT Stopped is accepted, the nonzero target interval is 2000 ms. If the NMT
frame is not confirmed, the executor waits at most 1000 ms for `0x701#04`, then
sends Pre-operational and verifies both targets zero. Any timeout, SDO abort,
EMCY, CAN error, stale feedback, cleanup error, unexpected motion, or abnormal
brake action stops the trial without retry.

The first physical attempt exposed a second startup defect. RK3588 candump and
independent JCAN both received `0x701#00`, but the executable required both a
boot-up and a current heartbeat before it could configure the temporary
heartbeat producer. Because the reviewed drive baseline is `0x1017:00=0`, no
heartbeat followed and the executable timed out before sending any SDO, NMT,
controlword, or target frame. The corrected startup accepts a valid heartbeat
without requiring prior observation of boot-up and uses the one-shot
`0x1017:00` upload as the bounded late-start online probe. Motion remains
inhibited until a fresh heartbeat is observed after the temporary producer is
enabled.

## Software evidence

Evidence directory: `evidence/p6_6_20260908_nmt_stop_preparation/`.
Startup-online correction: `evidence/p6_6_20260908_startup_online_fix/`.

| Check | Result |
|---|---|
| GCC 13.3 full CTest | 42/42 PASS |
| LLVM 22.1.8 ASan/UBSan full CTest | 42/42 PASS |
| Managed-vcan qualification matrix after correction | 1122 frames, prohibited 0, failures 0 |
| No-boot-up late-start SDO probe and heartbeat establishment | PASS |
| NMT Stopped timeout fail-closed regression | PASS |
| clang-format warnings-as-errors | PASS |
| clang-tidy bugprone scope | Four reviewed existing warnings; no new defect |
| Default Debug/Release and P5.6 artifact isolation | PASS |
| Locked, no-network RK3588 Debug qualification build | PASS, clean 69/69 |
| AArch64 interpreter, dependencies, symbol versions, and RPATH audit | PASS |
| JCAN read-only baseline | Serial `207F346D5650`; no warnings; no periodic tasks |

The corrected staged AArch64 ELF is
`out/staging/p66-online-269aaa0152cd/robot-control-zlac-qualification`,
SHA-256
`269aaa0152cd1a1d46a7d38dfe23fd214e31fa062479a10addf8ad27f4a5bd20`.
It is deployed without auto-start at
`/tmp/robot-control-qualifications/p66-online-269aaa0152cd/robot-control-zlac-qualification`
on RK3588, with an exact target-side checksum match.

## Physical HIL result

The final authorized same-session trial is preserved in
`evidence/p6_6_20260908_nmt_stop_trial_5/`. RK3588 and independent JCAN each
captured the same 185 arbitration-ID/payload frames with zero JCAN drops or
warnings. The late-start `0x1017:00` upload established node-1 liveness, the
temporary 500 ms heartbeat was confirmed, and the right-axis `+5 rpm` write
was followed by `000#0201` after 2000.879 ms. `0x701#04` arrived 104.323 ms
later. Both targets were written zero within 110.107 ms of NMT Stop; measured
right-axis velocity reached zero by 715.450 ms. Cleanup reached CiA402 Ready
to Switch On and NMT Pre-operational, then restored `0x200F:00=1` and
`0x1017:00=0` by 861.441 ms.

From the chassis right side, the operator observed the right wheel rotating
counter-clockwise and stopping after approximately two seconds. The left wheel stayed
stationary and the mechanical brake had no abnormal action or sound. No EMCY,
SDO abort, malformed frame, prohibited CAN frame, residual owner, or JCAN
cleanup error was present.

The earlier unpowered setup attempt remains classified separately and is not a
product failure or passing HIL attempt. It left `can0` reporting
`ERROR-PASSIVE`; the final trial retained that reported state with live TX/RX
error counters at zero and no cumulative counter advance. The complete
acknowledged exchange passed, while this controller-state inconsistency remains
a named residual for later interface-loss or electrical bus-off work.

## Executed HIL trial

- Target: RK3588 `can0`, ZLAC8015D V4 node 1, JCAN serial `207F346D5650`.
- Physical channel: `0x60FF:02`, mapped by P6.5 to the right wheel.
- Motion: `+5 rpm` for 2000 ms, once; `0x60FF:01=0` throughout.
- Stop stimulus: exactly one node-1 NMT Stopped frame `000#0201`.
- Heartbeat support: temporary volatile `0x1017:00=500 ms`, restored to 0.
- Required response: a newer `0x701#04`, followed by verified zero targets and
  safe cleanup.
- Startup: boot-up observation is optional. After a one-second passive window,
  one exact `0x601` upload of `0x1017:00` may establish SDO liveness.
- Capture: RK3588 raw CAN plus blocking independent JCAN capture starts before
  the corrected executor. The drive may already be powered.
- Observation: operator remains on the chassis right side and reports right-wheel
  direction and stop, left-wheel motion, and any brake action or sound.
- Recovery: independent power cut remains immediately available.

The final trial used the reviewed raised, unloaded fixture with unchanged
wiring, an unobstructed wheel, and an independent power cut available. The
operator explicitly authorized this exact one-shot stimulus after both captures
were ready.
