#!/usr/bin/env python3
"""Enable the exact one-shot X1 authorization without operating CAN."""
import hashlib
import json
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
BASE = "/tmp/robot-control-qualifications/emergency-input-zero-v2-bb4f44251672"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
source = OUT / "authorization.json"
authorization = json.loads(source.read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
assert authorization["date_utc"] == "2026-09-15" and authorization["user_confirmation"] == "授权"
assert subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert subprocess.run([*SSH, "test ! -e " + BASE + "/physical_once"], timeout=10).returncode == 0
remote_before = json.loads(subprocess.check_output([*SSH, "cat " + BASE + "/authorization.json"], text=True, timeout=10))
assert remote_before["authorized"] is False and remote_before["attempts"] == 0
subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                str(source), "robot-dev:" + BASE + "/authorization.json"], check=True, timeout=20)
digest = hashlib.sha256(source.read_bytes()).hexdigest()
remote_digest = subprocess.check_output([*SSH, "sha256sum " + BASE + "/authorization.json"], text=True, timeout=10).split()[0]
assert remote_digest == digest
(OUT / "authorization_sha256.txt").write_text(digest + "\n")
print("PASS: one X1 zero-motion pretest authorized; no CAN operation started")
