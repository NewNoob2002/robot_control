#!/usr/bin/env python3
"""Leave can0 down after the drive has been powered off at trial end."""
import json
import os
import subprocess
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/power-loss-v2-fc77b14ec242")
OUT = BASE / "final_interface_down_once"

def snapshot():
    """Read physical can0 state without application transmission."""
    return json.loads(subprocess.check_output(
        ["ip", "-j", "-details", "-statistics", "link", "show", "can0"], text=True, timeout=5))[0]

assert os.geteuid() == 0, "Run with sudo in the RK3588 terminal"
assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert input("Keep drive POWER OFF. Type POWER_OFF to leave can0 DOWN: " ).strip() == "POWER_OFF"
OUT.mkdir(exist_ok=False)
pre = snapshot()
(OUT / "before.json").write_text(json.dumps(pre, indent=2) + "\n")
subprocess.run(["ip", "link", "set", "dev", "can0", "down"], check=True, timeout=5)
post = snapshot()
(OUT / "after.json").write_text(json.dumps(post, indent=2) + "\n")
assert "UP" not in post["flags"]
(OUT / "result.json").write_text(json.dumps({"pass": True, "drive_power": "operator_confirmed_off", "can0_up": False}) + "\n")
print("DONE: can0 DOWN. Keep drive POWER OFF; no CAN transmission started.", flush=True)
