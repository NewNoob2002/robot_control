#!/usr/bin/env python3
"""Stage X1 trial scripts without enabling authorization or CAN."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
BASE = "/tmp/robot-control-qualifications/emergency-input-zero-fc77b14ec242"
INSTALL = "/opt/robot-control/staging/userspace-inhibit-fc77b14ec242"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
FILES = {name: OUT / name for name in ("authorization.json", "remote_trial.py", "trial_checks.py",
         "operator_watch.py", "restore_pretrial_can0.py", "finalize_can0_down.py")}
FILES["phase6_zero_motion_soak.py"] = ROOT / "scripts/hil/phase6_zero_motion_soak.py"

assert json.loads((OUT / "authorization.json").read_text())["authorized"] is False
identity = subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
remote_hash = subprocess.check_output([*SSH, "sha256sum " + shlex.quote(INSTALL + "/robot-control-zlac-qualification")],
                                      text=True, timeout=10).split()[0]
assert remote_hash == "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"
subprocess.run([*SSH, "mkdir -p " + shlex.quote(BASE)], check=True, timeout=10)
manifest = {"status": "PREPARED_NOT_AUTHORIZED", "target": identity, "base": BASE, "files": {}}
for name, source in FILES.items():
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                    str(source), f"robot-dev:{BASE}/{name}"], check=True, timeout=20)
    remote = subprocess.check_output([*SSH, "sha256sum " + shlex.quote(f"{BASE}/{name}")], text=True, timeout=10).split()[0]
    assert remote == digest
    manifest["files"][name] = digest
subprocess.run([*SSH, "chmod 0755 " + " ".join(shlex.quote(f"{BASE}/{name}") for name in FILES if name.endswith(".py"))],
               check=True, timeout=10)
subprocess.run([*SSH, "python3 -m py_compile " + " ".join(shlex.quote(f"{BASE}/{name}") for name in FILES if name.endswith(".py"))],
               check=True, timeout=10)
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("PASS X1 runner staged but not authorized; no CAN operation started")
