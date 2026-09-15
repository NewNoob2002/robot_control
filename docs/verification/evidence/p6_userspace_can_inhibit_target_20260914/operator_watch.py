#!/usr/bin/env python3
"""Show target-local timing for the authorized one-shot cable-loss trial."""
import json
import os
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/userspace-inhibit-fc77b14ec242")
OUT = BASE / "physical_once"
ELF_SHA = "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"

assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert not OUT.exists(), "One-shot trial already consumed"
print("Raised wheels, emergency stop ready, RK3588 CAN branch connected.")
print("Power the drive ON. Right wheel +5 rpm only; left wheel must remain still.")
print("On DISCONNECT NOW, unplug only the RK3588 CAN branch. Reconnect about 3 s later after the wheel stops.")
print("If motion is abnormal, use emergency stop and power OFF; do not reconnect.")
assert input("Type READY after drive power is ON and conditions are verified: ").strip() == "READY"
ready = BASE / "operator_ready.json"
ready.write_text(json.dumps({"pid": os.getpid(), "wall_time": time.time(), "elf_sha256": ELF_SHA}))
print("WATCH_READY: waiting for the single trial.", flush=True)
try:
    deadline = time.monotonic() + 180
    armed = False
    while time.monotonic() < deadline:
        if (OUT / "armed.json").exists() and not armed:
            event = json.loads((OUT / "armed.json").read_text())
            assert event["elf_sha256"] == ELF_SHA and time.time() - event["wall_time"] < 2
            print("\aDISCONNECT NOW: only the RK3588 CAN branch. Reconnect about 3 s later after normal stop.", flush=True)
            armed = True
        if (OUT / "wrapper_result.json").exists():
            result = json.loads((OUT / "wrapper_result.json").read_text())
            print("DONE: keep can0 DOWN. Power the drive OFF now, then report wheel behavior. " + json.dumps(result), flush=True)
            break
        time.sleep(0.05)
    else:
        raise TimeoutError("Readiness expired; no retry is authorized")
finally:
    ready.unlink(missing_ok=True)
