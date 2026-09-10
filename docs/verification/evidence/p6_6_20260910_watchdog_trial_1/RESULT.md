# Watchdog attempt 1: aborted during zero-target setup

The authorized attempt on 2026-09-10 did not reach the watchdog stimulus or
motion. RK3588 and silent JCAN captures agree exactly on all 170 frames:
33 target TX (11 downloads, 20 uploads, two NMT) and 137 RX. No nonzero target
or watchdog download occurred. Watchdog readback was zero.

Both axes continuously reported raw 0x1407 (Quick Stop Active). Setup Shutdown
was acknowledged in 0.334 ms but did not produce Ready to Switch On; the
two-second state wait timed out. The cleanup Shutdown acknowledgement arrived
in 0.300 ms, but its state wait also timed out. No Switch On or Enable Operation
was transmitted.

Both final target readbacks were zero, command application was restored to 1,
and heartbeat producer was restored to zero. Independent velocity was verified
zero in preflight; repeated TPDO packed velocity remained zero. Post-cleanup
independent velocity SDOs and a Pre-operational heartbeat were not verified.
State cleanup must therefore not be labelled fully passed.

The operator confirmed both wheels remained stationary with no abnormal brake
action or sound. CAN errors/drops and error-state counters stayed zero, adapter
configuration was unchanged, captures exited cleanly, and a later read-only
process check found no remaining executor/capture processes.

The watchdog requirement remains open: this was a startup-state failure, not
evidence that the watchdog itself failed. The operator was subsequently asked
to power-cycle the driver before requesting a fresh separately recorded attempt.
