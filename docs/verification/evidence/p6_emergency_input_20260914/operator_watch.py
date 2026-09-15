#!/usr/bin/env python3
"""Guide one zero-motion X1 emergency-input trial on RK3588."""
import json
import os
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/emergency-input-zero-fc77b14ec242")
OUT = BASE / "physical_once"
ELF_SHA = "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"

assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert not OUT.exists(), "One-shot emergency-input trial already consumed"
print("Drive power must initially be OFF; can0 was restored only while power was OFF.")
print("Raised and stopped wheels; X1 is the normally-open latching emergency-stop input.")
print("This pretest sends no target or controlword and must produce no wheel motion.")
print("Power the drive ON. Keep the RK3588 CAN branch connected and emergency power disconnect available.")
assert input("Type READY after these conditions are verified: " ).strip() == "READY"
ready = BASE / "operator_ready.json"
ready.write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time(), "elf_sha256": ELF_SHA}))
print("WATCH_READY: waiting for passive captures.", flush=True)
try:
    deadline = time.monotonic() + 300
    while not (OUT / "estop_ready.json").exists():
        if (OUT / "wrapper_result.json").exists():
            raise RuntimeError("Runner stopped before X1 action")
        assert time.monotonic() < deadline, "X1 readiness expired"
        time.sleep(0.05)
    print("Passive captures and zero-motion observation are active. Operate only X1.")
    assert input("Press and lock X1 now, then type ESTOP_LOCKED: " ).strip() == "ESTOP_LOCKED"
    (BASE / "operator_estop_locked.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    print("Holding X1 locked for 3 seconds...", flush=True)
    time.sleep(3)
    assert input("Rotate/pull X1 to its maintained reset state, then type RESET: " ).strip() == "RESET"
    (BASE / "operator_estop_reset.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    print("Observe both wheels for at least 5 seconds; neither may move or make an abnormal sound.", flush=True)
    time.sleep(5)
    assert input("If both wheels stayed still with no abnormal sound, type NO_MOTION: " ).strip() == "NO_MOTION"
    (BASE / "operator_no_motion.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    while not (OUT / "wrapper_result.json").exists():
        assert time.monotonic() < deadline, "Postflight expired"
        time.sleep(0.05)
    result = json.loads((OUT / "wrapper_result.json").read_text())
    print("FINAL POWER OFF NOW: switch drive power OFF and leave it OFF.", flush=True)
    assert input("After drive power is OFF and both wheels are stopped, type FINAL_POWER_OFF: " ).strip() == "FINAL_POWER_OFF"
    observation = {"pid": os.getpid(), "wall_time": time.time(), "wheels_stayed_still": True,
                   "abnormal_sound": False, "final_drive_power_off": True}
    (OUT / "operator_observation.json").write_text(json.dumps(observation, indent=2) + "\n")
    print("DONE: keep drive POWER OFF. " + json.dumps(result), flush=True)
finally:
    ready.unlink(missing_ok=True)
