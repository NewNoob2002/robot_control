#!/usr/bin/env python3
"""Stage the exact newly authorized record without starting CAN."""
import hashlib
import json
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
BASE = "/tmp/robot-control-qualifications/userspace-inhibit-retest-fc77b14ec242"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
source = OUT / "authorization.json"
authorization = json.loads(source.read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
assert authorization["user_confirmation"] == "确认并授权新的单次复测"
identity = subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
subprocess.run(
    ["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", str(source), f"robot-dev:{BASE}/authorization.json"],
    check=True,
    timeout=20,
)
expected = hashlib.sha256(source.read_bytes()).hexdigest()
actual = subprocess.check_output([*SSH, f"sha256sum {BASE}/authorization.json"], text=True, timeout=10).split()[0]
assert actual == expected
subprocess.run([*SSH, f"test ! -e {BASE}/physical_once"], check=True, timeout=10)
(OUT / "authorization_sha256.txt").write_text(expected + "  authorization.json\n")
print("PASS exact one-shot authorization staged; no CAN operation started")
