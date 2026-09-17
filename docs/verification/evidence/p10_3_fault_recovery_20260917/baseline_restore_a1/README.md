# Known A2-residual baseline restoration — PREPARED, not run

A3 confirmed2000:00=1000 instead of required0 and performed only8 SDO uploads.
Operator confirmed no CH6/X1, stationary wheels, no abnormal sound and OFF.
This separate mode restores the previously owned residual state before another
X1 trial. It does not accept residual settings as a new normal baseline.

Artifact4c7a757f578f774c0788221bd5d46da3247f53e9ca898001ef41b7f15472256f.
Arguments: --restore-zero-baseline --zero-feedback-tenths-rpm 10; duration argument
2000 is syntactic only: no control loop is constructed, preflight/cleanup have
their existing10s bounds. No enable, RPDO, nonzero target, fault reset or persistent
parameter write. Same can0/node1/500000, UART586D017868, silent JCAN207F346D5650.

The application requires current disabled state, zero controlword/targets, healthy
mode/fault, exact A2 residual RPDO/TPDO mappings, heartbeat500/watchdog1000 and605A5
before any write. It then claims only those known settings for existing cleanup:
zero SDO/Disable Voltage,>=150ms within±1rpm, NMT Pre-operational, original maps,
watchdog0/heartbeat0. Exactly18 writes and full original baseline readback required.
Foreign/partial states, nonzero targets, enabled axes or out-of-range speed reject
before writes. No automatic normal preflight fallback or retry.

Test-first cases plus actual virtual CLI restoration/rejection pass. Host Debug
and ASan/UBSan43/43 no skips; scoped static, locked cross101 steps, qualification
ELF audit and122 compiled-source hashes pass. Existing motion criteria unchanged.
START in the local terminal confirms fresh readiness; do not operate CH6/X1 during
this restoration. After exit power OFF and supply physical disposition. Dual
captures/independent18-write oracle must pass before a new X1 one-shot is prepared.

### Verified residual restoration — 2026-09-17

Restoration executed once with fresh local START and passed. Target343 frames
match JCAN435 at offset92; exact18 zero/Disable Voltage/map/timer writes, only NMT
Pre-operational, no RPDO/enable. Complete332-row trace; standstill holds159.951,
160.009 and162.003ms. All original baseline objects read back, including watchdog0,
heartbeat0, RPDO descriptor60600008 and empty TPDO2 mapping/timer. CAN errors/drops
unchanged. Operator local OFF confirms stationary wheels/no sound/OFF. Both markers
consumed. This closes residual restoration only, not X1/quick-stop acceptance.
