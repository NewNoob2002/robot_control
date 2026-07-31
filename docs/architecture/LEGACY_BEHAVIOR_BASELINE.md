# Legacy STM32 Behavioral Baseline

This document captures behavior to preserve conceptually while replacing
ThreadX/HAL/BSP mechanisms. The legacy repository is read-only:

`~/Desktop/workspace/STM32_PROJ/STM32G474_CANOPEN_Copy`

Inspected legacy commit: `c34042e68aa23fcd789a63a1693a507296935032`.

## Command sample contract

Every producer sample in Linux shall carry:

- source identity;
- source session/generation;
- monotonic receipt/capture timestamp;
- strictly newer sequence within its session;
- validity/health/failsafe flags;
- typed command values and explicit stop/neutral state.

A coherent sample is internally consistent and atomically observed. An invalid,
incoherent, stale, replayed, or out-of-session sample cannot authorize motion.

Evidence: `USER/Motion/control_arbiter.c:168-215`.

## Arbitration state table

| Current condition | Required output | Ownership effect |
|---|---|---|
| Inputs incoherent or selected intent invalid | zero, source `NONE` | revoke external authorization |
| Healthy SBUS with non-zero/manual run intent | bounded SBUS command | SBUS owns immediately; revoke external session |
| SBUS stop, lost, failsafe, or stale | zero | external is not implicitly restored |
| External stale/stop/invalid | zero | revoke external authorization |
| Recovery after SBUS/manual ownership | zero during dwell | require both sources zero and healthy as applicable |
| Zero dwell <150 ms | zero | no ownership switch |
| Zero dwell >=150 ms but no new rearm generation | zero | reject old external authorization |
| Zero dwell >=150 ms and fresh external generation/command | bounded external command | external may own |
| Any source switch | zero first | old source sequence/session cannot replay |

Evidence: `USER/Motion/control_arbiter.c:256-313` and
`Docs/CONTROL_ARBITRATION_SAFETY_DESIGN.md:58-123,159-177`.

The 150 ms dwell is the initial compatibility value from
`USER/Config/motion_config.h:7-22`; it becomes validated runtime configuration.

## Safety state model

Initial Linux states:

- `STARTUP_INHIBIT`
- `ZERO_HOLD`
- `NORMAL`
- `REMOTE_LOST`
- `REMOTE_RECOVERING`
- `EXTERNAL_COMMAND_LOST`
- `CANOPEN_UNAVAILABLE`
- `DRIVE_FAULT`
- `EMERGENCY_STOP`
- `DEGRADED`
- `SHUTDOWN`

### Universal invariants

1. Every state except authorized `NORMAL` produces zero target.
2. Startup and every re-enable transition send zero before enable.
3. Communication recovery restores observation, not motion authorization.
4. A latching reason is cleared only when its source condition is resolved and
   explicit recovery criteria pass.
5. A new authorization generation is required after any loss, source switch,
   drive fault, bus-off, process restart, or emergency stop.

### Fault-reset eligibility

Fault reset is allowed only when all are true:

- original fault/inhibit reasons have converged to a reset-eligible condition;
- both drive axes are observed in `FAULT`;
- selected command is current and zero;
- CAN link, NMT state, heartbeat, statusword and TPDO observations are current;
- vendor/device safety inputs are safe;
- the request uses a fresh reset/rearm generation.

Evidence: `USER/Motion/safety_manager.c:277-306`.

### Enable sequence

Required zero-target sequence:

```text
SHUTDOWN controlword
  -> observed READY_TO_SWITCH_ON
SWITCH_ON controlword
  -> observed SWITCHED_ON
ENABLE_OPERATION controlword
  -> observed OPERATION_ENABLED and requested mode active
```

No transition advances on delay alone. Each requires a newer status observation
and a bounded deadline. The legacy transition timeout baseline is 500 ms.

Evidence: `USER/Motion/safety_manager.c:317-339`,
`USER/CanopenApp/canopen_drive_executor.c:229-333`, and
`USER/Config/motion_config.h:7-22`.

## CiA402 decode baseline

Decode with standard masks while retaining the full raw value:

| State | `(statusword & 0x006F)` expected |
|---|---:|
| Not ready to switch on | `0x0000` |
| Switch on disabled | `0x0040` |
| Ready to switch on | `0x0021` |
| Switched on | `0x0023` |
| Operation enabled | `0x0027` |
| Quick stop active | `0x0007` |
| Fault reaction active | `0x000F` |
| Fault | `0x0008` |

An unknown masked value is `UNKNOWN`, never treated as enabled. For the
ZLAC8015D dual-axis status representation, decode low/high halves independently
and do not label them left/right until hardware mapping evidence exists.

## Communication-loss behavior

| Event | Immediate domain effect | Recovery gate |
|---|---|---|
| `can0` missing/read failure | invalidate all node/PDO observations; zero/inhibit | reopen + full node/drive qualification + new generation |
| bus-off | zero then configured quick-stop/disable; revoke authorization | interface recovery + NMT/HB/PDO/mode/state qualification |
| heartbeat stale | node unavailable; zero coupled axes | new boot/HB and full qualification |
| TPDO/status stale | feedback invalid; zero coupled axes | fresh status/mode plus safety rearm |
| EMCY/drive fault | latch raw/decoded evidence; inhibit | gated reset procedure |
| SBUS lost/failsafe/stale | zero; revoke manual validity | valid frames + neutral confirmation |
| ROS2/external stale | zero; revoke external generation | new session/generation and handover dwell |
| process restart | no persisted authorization | full startup qualification |
| SIGTERM | stop command intake, zero, bounded safe drive state, close | process exits |
| crash/SIGKILL/power loss | application cleanup unavailable | drive communication-loss protection and physical safety path |

## SBUS parsing baseline

- Streaming parser consumes arbitrary byte chunks.
- Frame length is 25 bytes.
- Header/footer must match the supported receiver protocol.
- Noise, partial frames, and bad footer trigger resynchronization without
  publishing a valid command.
- `frame_lost` or `failsafe` clears health immediately.
- Stale timeout is independent from the receiver flags.
- Link recovery requires configured consecutive valid frames and neutral/center
  confirmation before motion can be authorized.

Evidence: `USER/Input/sbus_parser.c:78-132`.

## Explicitly not preserved

- ThreadX task count, priority numbers, tick conversions, and mailboxes;
- FDCAN ISR/ring/router topology;
- UART DMA buffer ownership;
- TIM6 timekeeping;
- STM32 HAL/BSP errors, GPIO and pinmux;
- EasyLogger/RTT output mechanism.

Their safety intent is preserved through Linux execution ownership, monotonic
timestamps, typed observations, diagnostics, systemd, SocketCAN, tty, and
drive-side loss protection.

