# Single-RPDO physical verification

Left and right +5 rpm, 3000 ms trials each ran once with artifact 0e835fd8a9bc.
Both operators' observations are recorded: selected wheel only, normal stop,
other wheel stationary, no abnormal sound. Each trial sent exactly two RPDOs:
Operation Enabled plus packed nonzero target, then Shutdown plus packed zero.
Independent and packed SDO readings and TPDO readings track the selected wheel;
all opposite-wheel samples are zero. No CAN errors or drops occurred.

Left: 290 matching frames, 96 SDO transactions, 101 host TX frames,
target-to-zero 3001.002 ms. Right: 292 matching frames, 96 SDO transactions,
101 host TX frames, target-to-zero 3000.994 ms. Timing is measured, not hard RT.
Both factory RPDO1 mappings and volatile heartbeat settings were restored.
No EEPROM writes, SYNC frames, automatic physical retries or simultaneous
nonzero wheel commands occurred. The current source includes later stop/loss
changes; this manifest preserves the exact earlier artifact used for these runs.

Run analyze.py / analyze_right.py to regenerate comparison and final readback
checks. Do not rerun either one-shot physical coordinator.
