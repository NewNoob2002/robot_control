#!/usr/bin/env python3
"""Coordinate one drive-power loss/restoration trial on the RK3588."""
import json
import os
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/power-loss-v2-fc77b14ec242")
OUT = BASE / "physical_once"
ELF_SHA = "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"

assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert not OUT.exists(), "One-shot power-loss trial already consumed"
print("Drive power must initially be OFF; can0 was restored only while power was OFF.")
print("Raised wheels, emergency stop ready, RK3588 CAN branch connected.")
print("Power the drive ON. Right wheel +5 rpm only; left wheel must remain still.")
assert input("Type READY after drive power is ON and conditions are verified: " ).strip() == "READY"
ready = BASE / "operator_ready.json"
ready.write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time(), "elf_sha256": ELF_SHA}))
print("WATCH_READY: waiting for passive captures.", flush=True)
try:
    deadline = time.monotonic() + 300
    while not (BASE / "capture_ready.json").exists():
        assert time.monotonic() < deadline, "Capture readiness expired"
        time.sleep(0.05)
    print("Passive captures are ready. Put your hand on ONLY the drive power switch/disconnect.")
    print("After typing ARMED, watch THIS terminal continuously and do not wait for any chat message.")
    assert input("Type ARMED when drive power can be removed immediately: " ).strip() == "ARMED"
    (BASE / "operator_armed.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    while not (OUT / "power_cut_armed.json").exists():
        if (OUT / "wrapper_result.json").exists():
            raise RuntimeError("Runner stopped before power-cut arm")
        assert time.monotonic() < deadline, "Power-cut arm expired"
        time.sleep(0.01)
    event = json.loads((OUT / "power_cut_armed.json").read_text())
    assert event["elf_sha256"] == ELF_SHA and time.time() - event["wall_time"] < 2
    print("\aPOWER OFF NOW: remove only drive power. Keep RK3588 and JCAN powered.", flush=True)
    assert input("Immediately after drive power is OFF, type POWER_OFF: " ).strip() == "POWER_OFF"
    (BASE / "operator_power_cut.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    print("Holding drive power OFF for 3 seconds after the stop confirmation...", flush=True)
    time.sleep(3)
    print("\aPOWER ON NOW: restore drive power once; do not touch CAN wiring.", flush=True)
    assert input("Immediately after drive power is ON, type POWER_ON: " ).strip() == "POWER_ON"
    (BASE / "operator_power_restore.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    print("Observe both wheels for at least 5 seconds; neither wheel may restart.", flush=True)
    time.sleep(5)
    assert input("If there was no restart or abnormal sound, type NO_RESTART: " ).strip() == "NO_RESTART"
    (BASE / "operator_no_restart.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    print("\aFINAL POWER OFF NOW: remove drive power and leave it OFF.", flush=True)
    assert input("After drive power is OFF, type FINAL_POWER_OFF: " ).strip() == "FINAL_POWER_OFF"
    (BASE / "operator_final_power_off.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    while not (OUT / "wrapper_result.json").exists():
        assert time.monotonic() < deadline, "Postflight expired"
        time.sleep(0.05)
    result = json.loads((OUT / "wrapper_result.json").read_text())
    print("DONE: keep drive POWER OFF. " + json.dumps(result), flush=True)
finally:
    ready.unlink(missing_ok=True)
