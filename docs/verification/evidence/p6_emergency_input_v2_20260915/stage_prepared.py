#!/usr/bin/env python3
"""Stage X1 trial scripts without enabling authorization or CAN."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
BASE = "/tmp/robot-control-qualifications/emergency-input-zero-v2-bb4f44251672"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
FILES = {name: OUT / name for name in ("authorization.json", "remote_trial.py", "trial_checks.py",
         "operator_watch.py", "restore_pretrial_can0.py", "finalize_can0_down.py", "prepare_privileged.sh")}
FILES["phase6_zero_motion_soak.py"] = ROOT / "scripts/hil/phase6_zero_motion_soak.py"
FILES["robot-control-zlac-qualification"] = (
    ROOT / "out/build/cross/userspace-inhibit-container-qualification/tools/zlac_qualification/robot-control-zlac-qualification")

assert json.loads((OUT / "authorization.json").read_text())["authorized"] is False
identity = subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
assert hashlib.sha256(FILES["robot-control-zlac-qualification"].read_bytes()).hexdigest() == (
    "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7")
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
subprocess.run([*SSH, "chmod 0755 " + shlex.quote(BASE + "/prepare_privileged.sh")
                + " " + shlex.quote(BASE + "/robot-control-zlac-qualification")], check=True, timeout=10)
subprocess.run([*SSH, "python3 -m py_compile " + " ".join(shlex.quote(f"{BASE}/{name}") for name in FILES if name.endswith(".py"))],
               check=True, timeout=10)
remote_elf = subprocess.check_output([*SSH, "sha256sum " + shlex.quote(BASE + "/robot-control-zlac-qualification")],
                                     text=True, timeout=10).split()[0]
assert remote_elf == "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7"
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("PASS repaired X1 runner and ELF staged but not authorized; no CAN operation started")
