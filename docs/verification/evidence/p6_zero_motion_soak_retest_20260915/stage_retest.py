#!/usr/bin/env python3
"""Stage the authorized soak retest without operating CAN."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
BASE = "/tmp/robot-control-qualifications/zero-motion-soak-bb4f44251672"
SCRIPT_SHA = "4f2fddc9cce66001f7fffedd0ed2483ae0b332efe9ca702b0d223c07e16e93a6"
ELF = "/opt/robot-control/staging/emergency-input-zero-v2-bb4f44251672/robot-control-zlac-qualification"
ELF_SHA = "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
FILES = {
    "authorization_retest.json": OUT / "authorization.json",
    "run_retest_once.py": OUT / "run_retest_once.py",
    "restore_retest_can0.py": OUT / "restore_retest_can0.py",
    "finalize_retest_can0_down.py": OUT / "finalize_retest_can0_down.py",
}
authorization = json.loads((OUT / "authorization.json").read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1 and authorization["hours"] == 3
assert subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert subprocess.check_output([*SSH, "sha256sum " + shlex.quote(BASE + "/phase6_zero_motion_soak.py")], text=True, timeout=10).split()[0] == SCRIPT_SHA
assert subprocess.check_output([*SSH, "sha256sum " + shlex.quote(ELF)], text=True, timeout=10).split()[0] == ELF_SHA
assert subprocess.run([*SSH, "test -f " + BASE + "/overnight_once.tar.gz"], timeout=10).returncode == 0
assert subprocess.run([*SSH, "test ! -e " + BASE + "/overnight_retest_once"], timeout=10).returncode == 0
manifest = {"status": "AUTHORIZED_RETEST_STAGED", "files": {}}
for name, source in FILES.items():
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                    str(source), f"robot-dev:{BASE}/{name}"], check=True, timeout=20)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    assert subprocess.check_output([*SSH, "sha256sum " + shlex.quote(f"{BASE}/{name}")], text=True, timeout=10).split()[0] == digest
    manifest["files"][name] = digest
subprocess.run([*SSH, "chmod 0755 " + " ".join(shlex.quote(f"{BASE}/{name}") for name in FILES if name.endswith(".py"))], check=True, timeout=10)
subprocess.run([*SSH, "python3 -m py_compile " + " ".join(shlex.quote(f"{BASE}/{name}") for name in FILES if name.endswith(".py"))], check=True, timeout=10)
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("PASS: authorized three-hour soak retest staged; no CAN operation started")
