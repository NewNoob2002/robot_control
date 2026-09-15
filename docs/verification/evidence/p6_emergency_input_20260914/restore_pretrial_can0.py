#!/usr/bin/env python3
"""Restore can0 once before the X1 trial, only while drive power is off."""
import json
import os
import subprocess
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/emergency-input-zero-fc77b14ec242")
OUT = BASE / "pretrial_interface_restore_once"


def snapshot():
    """Read physical can0 state without application transmission."""
    return json.loads(subprocess.check_output(
        ["ip", "-j", "-details", "-statistics", "link", "show", "can0"], text=True, timeout=5))[0]


assert os.geteuid() == 0, "Run with sudo in the RK3588 terminal"
assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert input("Keep drive POWER OFF. Type POWER_OFF to restore can0 once: " ).strip() == "POWER_OFF"
pre = snapshot()
assert pre["mtu"] == 16 and pre["linkinfo"]["info_data"]["bittiming"]["bitrate"] == 500000
OUT.mkdir(exist_ok=False)
(OUT / "before.json").write_text(json.dumps(pre, indent=2) + "\n")
try:
    subprocess.run(["ip", "link", "set", "dev", "can0", "down"], check=True, timeout=5)
    subprocess.run(["ip", "link", "set", "dev", "can0", "up"], check=True, timeout=5)
    time.sleep(0.2)
    post = snapshot()
    (OUT / "after.json").write_text(json.dumps(post, indent=2) + "\n")
    info = post["linkinfo"]["info_data"]
    assert "UP" in post["flags"] and info["state"] == "ERROR-ACTIVE"
    assert info["bittiming"]["bitrate"] == 500000 and all(value == 0 for value in info.get("berr_counter", {}).values())
except BaseException:
    subprocess.run(["ip", "link", "set", "dev", "can0", "down"], timeout=5, check=False)
    raise
print("DONE: can0 ERROR-ACTIVE. Keep drive POWER OFF; no motor test started.", flush=True)
