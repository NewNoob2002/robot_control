#!/usr/bin/env python3
"""Coordinate the operator locally so the cable is ready before motion starts."""
import json
import os
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/userspace-inhibit-retest-fc77b14ec242")
OUT = BASE / "physical_once"
ELF_SHA = "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"

assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert not OUT.exists(), "One-shot retest already consumed"
print("Raised wheels, emergency stop ready, RK3588 CAN branch connected.")
print("Power the drive ON. Right wheel +5 rpm only; left wheel must remain still.")
print("The test will pause before motion so you can place a hand on the RK3588 CAN connector.")
assert input("Type READY after drive power is ON and conditions are verified: ").strip() == "READY"
ready = BASE / "operator_ready.json"
ready.write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time(), "elf_sha256": ELF_SHA}))
print("WATCH_READY: waiting for passive captures.", flush=True)
try:
    deadline = time.monotonic() + 240
    while not (BASE / "capture_ready.json").exists():
        assert time.monotonic() < deadline, "Capture readiness expired"
        time.sleep(0.05)
    print("Passive captures are ready. Put your hand on ONLY the RK3588 CAN branch connector.")
    assert input("Type ARMED when you can unplug immediately: ").strip() == "ARMED"
    (BASE / "operator_armed.json").write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time()}))
    while not (OUT / "armed.json").exists():
        if (OUT / "wrapper_result.json").exists():
            raise RuntimeError("Runner stopped before motion arm")
        assert time.monotonic() < deadline, "Motion arm expired"
        time.sleep(0.01)
    event = json.loads((OUT / "armed.json").read_text())
    assert event["elf_sha256"] == ELF_SHA and time.time() - event["wall_time"] < 2
    print("\aDISCONNECT NOW: unplug only the RK3588 CAN branch.", flush=True)
    print("After the right wheel stops normally, wait about 3 s, reconnect the same branch, and observe no restart.")
    confirmation = input("After reconnect and no restart, type RECONNECTED: ").strip()
    assert confirmation == "RECONNECTED"
    (BASE / "operator_sequence.json").write_text(json.dumps({
        "pid": os.getpid(),
        "wall_time": time.time(),
        "confirmation": confirmation,
    }))
    while not (OUT / "wrapper_result.json").exists():
        assert time.monotonic() < deadline, "Postflight expired"
        time.sleep(0.05)
    result = json.loads((OUT / "wrapper_result.json").read_text())
    print("DONE: keep can0 DOWN. Power the drive OFF now, then report wheel behavior. " + json.dumps(result), flush=True)
finally:
    ready.unlink(missing_ok=True)
