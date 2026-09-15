#!/usr/bin/env python3
"""Restore can0 once before the trial, only while drive power is off."""
import json
import os
import subprocess
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/power-loss-v2-fc77b14ec242")
OUT = BASE / "pretrial_interface_restore_once"

def snapshot():
    """Read physical can0 state without application transmission."""
    return json.loads(subprocess.check_output(
        ["ip", "-j", "-details", "-statistics", "link", "show", "can0"], text=True, timeout=5))[0]

assert os.geteuid() == 0, "Run with sudo in the RK3588 terminal"
assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert input("Keep drive POWER OFF. Type POWER_OFF to restore can0 once: " ).strip() == "POWER_OFF"
for entry in Path("/proc").iterdir():
    if not entry.name.isdigit():
        continue
    try:
        name = (entry / "exe").resolve(strict=True).name
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        continue
    assert name not in ("cansend", "cangen", "robot-control-zlac-qualification", "robot-control-canopen-commission"), name
pre = snapshot()
assert pre["mtu"] == 16 and pre["linkinfo"]["info_data"]["bittiming"]["bitrate"] == 500000
OUT.mkdir(exist_ok=False)
(OUT / "before.json").write_text(json.dumps(pre, indent=2) + "\n")
(OUT / "operator_confirmation.txt").write_text("POWER_OFF\n")
try:
    subprocess.run(["ip", "link", "set", "dev", "can0", "down"], check=True, timeout=5)
    subprocess.run(["ip", "link", "set", "dev", "can0", "up"], check=True, timeout=5)
    time.sleep(0.2)
    post = snapshot()
    (OUT / "after.json").write_text(json.dumps(post, indent=2) + "\n")
    info = post["linkinfo"]["info_data"]
    assert "UP" in post["flags"] and info["state"] == "ERROR-ACTIVE"
    assert info["bittiming"]["bitrate"] == 500000 and info["restart_ms"] == pre["linkinfo"]["info_data"]["restart_ms"]
    assert all(value == 0 for value in info.get("berr_counter", {}).values())
    (OUT / "result.json").write_text(json.dumps({"pass": True, "drive_power": "operator_confirmed_off", "application_tx": False}) + "\n")
except BaseException:
    subprocess.run(["ip", "link", "set", "dev", "can0", "down"], timeout=5, check=False)
    (OUT / "result.json").write_text(json.dumps({"pass": False, "retry_authorized": False}) + "\n")
    raise
print("DONE: can0 ERROR-ACTIVE. Keep drive POWER OFF; no motor test started.", flush=True)
