# ADR-0004: Single-Owner Execution Model

- Status: Accepted
- Date: 2026-07-31

## Context

ROS2, SBUS, CANopen feedback, safety faults, and process lifecycle are concurrent
inputs. Allowing callbacks to write motor targets independently would create
races and unsafe ownership transitions.

## Decision

Use one control-cycle owner for command arbitration, robot safety, drive intent,
and final action publication. Initial control period is 10 ms (100 Hz).

Initial execution contexts:

1. CANopen event/deadline processing;
2. the control cycle;
3. SBUS tty reader/parser source;
4. an optional ROS2 executor added later.

Producers publish immutable latest-value snapshots containing monotonic
timestamp, sequence, validity, and session/generation. The control owner alone
mutates arbiter and safety state. CANopenNode objects and SDO transactions are
owned by the CANopen context. A bounded latest-action mailbox connects control
to CANopen; no unbounded motion queue is allowed.

Start with mutex-protected snapshot copies. Adopt lock-free structures only
after measurement. CANopen deadlines may use a 1 ms/event-driven cadence
without running robot policy at 1 kHz.

## Alternatives

- One thread per module: rejected due to unnecessary scheduling and shared-state
  complexity.
- ROS2 callback ownership: rejected because ROS2 failure must be isolated.
- Fully lock-free design initially: rejected as premature and harder to verify.
- Single process loop for everything: viable for an early spike, but tty/ROS2
  blocking and distinct CAN deadlines make explicit ownership safer.

## Consequences

- All actuator authorization has one serializable trace.
- Cross-thread interfaces require clear snapshot age/generation semantics.
- SDO and logging cannot block the control cycle.
- Scheduling policy remains measurable rather than claimed hard real-time.

## Verification

- Tests prove that no producer can bypass arbitration and safety.
- Record p50/p95/p99/max wake lateness, cycle time, action-to-RPDO latency, and
  missed deadlines before considering SCHED_FIFO, affinity, or PREEMPT_RT.

