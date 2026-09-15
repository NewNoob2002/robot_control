#!/usr/bin/env python3
"""Guide one moving X1 emergency-input trial on RK3588."""
import json
import os
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/emergency-input-moving-bb4f44251672")
OUT = BASE / "physical_once"
ELF_SHA = "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7"


def marker(name, values=None):
    """Write one operator event owned by this watcher."""
    data = {"pid": os.getpid(), "wall_time": time.time()}
    if values:
        data.update(values)
    (BASE / name).write_text(json.dumps(data, indent=2) + "\n")


def wait_for(name, deadline):
    """Wait for one runner marker or fail when the runner has stopped."""
    path = OUT / name
    while not path.exists():
        if (OUT / "wrapper_result.json").exists():
            raise RuntimeError("runner stopped before " + name)
        assert time.monotonic() < deadline, name + " deadline"
        time.sleep(0.05)
    return json.loads(path.read_text())


assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert not OUT.exists(), "One-shot moving X1 trial already consumed"
print("Drive power must initially be OFF; can0 was restored only while power was OFF.")
print("Raised and stopped wheels; X1 reset; emergency drive-power disconnect immediately available.")
print("The single stimulus is right wheel +5 rpm for at most 3 s; left wheel target remains zero.")
print("Power the drive ON. Keep the RK3588 CAN branch connected.")
assert input("Type READY after these conditions are verified: ").strip() == "READY"
marker("operator_ready.json", {"elf_sha256": ELF_SHA})
print("WATCH_READY: waiting for passive captures.", flush=True)
deadline = time.monotonic() + 240
try:
    wait_for("armed_ready.json", deadline)
    print("Passive captures are ready. Put your hand on X1 so it can be pressed immediately.")
    assert input("Type ARMED when X1 can be pressed without delay: ").strip() == "ARMED"
    marker("operator_armed.json")
    wait_for("press_x1_now.json", deadline)
    print("PRESS X1 NOW: press and leave X1 locked.", flush=True)
    assert input("After X1 is locked, type ESTOP_LOCKED: ").strip() == "ESTOP_LOCKED"
    marker("operator_estop_locked.json")
    wait_for("safe_to_reset_x1.json", deadline)
    print("SAFE TO RESET X1: zero target and stopped feedback were verified while X1 stayed locked.", flush=True)
    assert input("Rotate/pull X1 to its maintained reset state, then type RESET: ").strip() == "RESET"
    marker("operator_estop_reset.json")
    print("Observe both wheels for at least 5 seconds; neither wheel may restart or make an abnormal sound.", flush=True)
    time.sleep(5)
    prompt = "If the right wheel stopped normally, the left stayed still, and there was no restart or abnormal sound, type ACCEPTED: "
    assert input(prompt).strip() == "ACCEPTED"
    marker("operator_no_restart.json")
    while not (OUT / "wrapper_result.json").exists():
        assert time.monotonic() < deadline, "postflight deadline"
        time.sleep(0.05)
    result = json.loads((OUT / "wrapper_result.json").read_text())
    print("FINAL POWER OFF NOW: switch drive power OFF and leave it OFF.", flush=True)
    assert input("After drive power is OFF and both wheels are stopped, type FINAL_POWER_OFF: ").strip() == "FINAL_POWER_OFF"
    observation = {"pid": os.getpid(), "wall_time": time.time(), "right_wheel_stopped_normally": True,
                   "left_wheel_stayed_still": True, "restart_observed": False,
                   "abnormal_sound": False, "x1_reset": True, "final_drive_power_off": True}
    (OUT / "operator_observation.json").write_text(json.dumps(observation, indent=2) + "\n")
    print("DONE: keep drive POWER OFF. " + json.dumps(result), flush=True)
except BaseException:
    print("STOP: use X1 or the independent disconnect if needed, then switch drive power OFF.", flush=True)
    if input("After drive power is OFF and both wheels are stopped, type FINAL_POWER_OFF: ").strip() == "FINAL_POWER_OFF":
        marker("operator_failure_power_off.json", {"final_drive_power_off": True})
    raise
finally:
    (BASE / "operator_ready.json").unlink(missing_ok=True)
