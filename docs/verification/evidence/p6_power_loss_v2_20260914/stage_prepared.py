#!/usr/bin/env python3
"""Stage power-loss scripts without enabling authorization or CAN."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
BASE = "/tmp/robot-control-qualifications/power-loss-v2-fc77b14ec242"
INSTALL = "/opt/robot-control/staging/userspace-inhibit-fc77b14ec242"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
NAMES = ("authorization.json", "remote_trial.py", "trial_checks.py", "operator_watch.py",
         "restore_pretrial_can0.py", "finalize_can0_down.py")

assert json.loads((OUT / "authorization.json").read_text())["authorized"] is False
identity = subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
remote_hashes = subprocess.check_output([*SSH, "sha256sum " + shlex.quote(INSTALL + "/robot-control-zlac-qualification")
                                         + " " + shlex.quote(INSTALL + "/robot-control-can-interface-inhibitor")],
                                        text=True, timeout=10).splitlines()
assert remote_hashes[0].split()[0] == "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"
assert remote_hashes[1].split()[0] == "c2425001cbe583e3bb1e3307d4145e95cafd722d150154ff727fe519a80b1a5b"
subprocess.run([*SSH, "mkdir -p " + shlex.quote(BASE)], check=True, timeout=10)
manifest = {"status": "PREPARED_NOT_AUTHORIZED", "target": identity, "base": BASE, "files": {}}
for name in NAMES:
    source = OUT / name
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                    str(source), f"robot-dev:{BASE}/{name}"], check=True, timeout=20)
    remote = subprocess.check_output([*SSH, "sha256sum " + shlex.quote(f"{BASE}/{name}")], text=True, timeout=10).split()[0]
    assert remote == digest
    manifest["files"][name] = digest
subprocess.run([*SSH, "chmod 0755 " + " ".join(shlex.quote(f"{BASE}/{name}") for name in NAMES if name.endswith(".py"))],
               check=True, timeout=10)
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("PASS power-loss runner staged but not authorized; no CAN operation started")
