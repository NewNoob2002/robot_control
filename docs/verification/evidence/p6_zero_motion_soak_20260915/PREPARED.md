# Phase 6 three-hour zero-motion soak

Prepared: 2026-09-15.

Status: **AUTHORIZED ONCE FOR THREE HOURS, NOT RUN**. Drive power is OFF, X1 is reset,
and can0 is DOWN/STOPPED.

The fixed one-shot entry runs three hours of repeated 60-second disabled
`--manual-tpdo` lifecycles. X1 must be locked before drive power is applied and
must remain locked until the drive is powered off after completion or failure.
The qualification operation sends no target, RPDO, or controlword. It only uses
the reviewed heartbeat setting, NMT Operational/Pre-operational, and read-only
SDO/TPDO observations.

The run stores continuous candump, every application cycle, pre/post interface
statistics, per-cycle results, a manifest, final result, and one tar.gz archive
under the target output directory. The same script performs next-day offline
analysis on either the directory or archive.

Any failed cycle, sampled nonzero speed, CAN error, increased error/drop counter,
or signal stops further cycles. The current child receives SIGTERM and gets its
bounded cleanup opportunity. X1 remains the independent hardware inhibit.

After the script exits, the operator keeps X1 locked, powers the drive off, and
runs the dedicated finalizer to set can0 DOWN. Only then may X1 be reset.
