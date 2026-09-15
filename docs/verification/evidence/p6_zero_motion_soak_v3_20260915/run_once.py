#!/usr/bin/env python3
"""Execute the authorized three-hour zero-motion soak v3 once."""
import hashlib
import json
import os
import sys
import time
from pathlib import Path

BASE = Path("/tmp/robot-control-qualifications/zero-motion-soak-v3-bb4f44251672")
SCRIPT = BASE / "phase6_zero_motion_soak.py"
SCRIPT_SHA = "4f2fddc9cce66001f7fffedd0ed2483ae0b332efe9ca702b0d223c07e16e93a6"
ELF = Path("/opt/robot-control/staging/emergency-input-zero-v2-bb4f44251672/robot-control-zlac-qualification")
ELF_SHA = "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7"
AUTHORIZATION = BASE / "authorization.json"
OUTPUT = BASE / "overnight_once"

authorization = json.loads(AUTHORIZATION.read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
assert authorization["hours"] == 3 and authorization["output"] == str(OUTPUT)
assert hashlib.sha256(SCRIPT.read_bytes()).hexdigest() == SCRIPT_SHA
assert hashlib.sha256(ELF.read_bytes()).hexdigest() == ELF_SHA
assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert not OUTPUT.exists(), "one-shot soak retest already consumed"
marker = BASE / "run_once.marker"
marker.write_text(json.dumps({"wall_time": time.time(), "pid": os.getpid()}, indent=2) + "\n")
print("SETTLING: keep both raised wheels untouched for 5 seconds.", flush=True)
time.sleep(5)
os.execv(sys.executable, [sys.executable, str(SCRIPT), "run", "--hours", "3", "--interface", "can0",
                          "--elf", str(ELF), "--output", str(OUTPUT),
                          "--confirm", "X1_LOCKED_WHEELS_RAISED"])
