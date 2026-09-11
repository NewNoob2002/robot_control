# Synchronous packed-target feedback — 2026-09-11

One authorized left +5 rpm / right zero, 3000 ms trial completed. Mode 0x200F
was read as 1 and never written. Targets used 0x60FF:03. Existing TPDO1
mapping was retained. Six running independent-left / packed-left / nearest
TPDO-left raw samples were 49/49/46, 46/46/47, 52/53/53, 52/52/51,
50/50/50, 47/47/46 (0.1 rpm units); all sampled right values were zero.
Sequential SDO reads and nearby TPDOs are not simultaneous measurements.

234 frames match exactly across RK3588 and silent JCAN captures; 68 SDO
transactions plus two NMT frames reconcile with 70 target TX packets.
Target-to-zero was 3001.000 ms (measured, not a hard-real-time guarantee).
The application and coordinator exited zero; cleanup and error/drop checks
passed. The operator confirmed left-only rotation, normal stop, stationary right wheel and no abnormal sound; see left/operator_observation.json.
No retry, EEPROM write, PDO mapping write or RPDO transmission occurred.

The asynchronous enabled trial previously reported independent motion but
zero packed feedback. This synchronous packed-target trial restores packed
feedback in both SDO and TPDO, supporting the selected repair route. It
changes both command mode and target object; it is not proof of the isolated
effect of 0x200F alone or of every firmware revision.

Validation: host 57/57; ASan/UBSan 57/57; clean pinned GCC11.4 aarch64 build
and application/test ELF audit; target isolated-vcan pass, prohibited=0 and
failures=0; clang-tidy exit0, no errors, nine advisories (including the new
identical upload-size branches). Initial host failure is retained: the
deceleration fixture left TPDO speed nonzero after SDO reached zero. Cleanup
now waits for a zero-speed status TPDO under the original transition bound.
Startup still rejects any nonzero TPDO speed immediately.

Run analyze.py to regenerate the capture comparison; do not rerun the
one-shot physical coordinator. Manifest records exact staged hashes.
RPDO and remaining stop/loss tests are not signed off by this result.
