#!/usr/bin/env python3
"""Stage the unauthorized three-hour soak runner without operating CAN."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
BASE = "/tmp/robot-control-qualifications/zero-motion-soak-bb4f44251672"
ELF = "/opt/robot-control/staging/emergency-input-zero-v2-bb4f44251672/robot-control-zlac-qualification"
ELF_SHA = "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
FILES = {name: OUT / name for name in ("authorization.json", "run_once.py",
         "restore_pretrial_can0.py", "finalize_can0_down.py")}
FILES["phase6_zero_motion_soak.py"] = ROOT / "scripts/hil/phase6_zero_motion_soak.py"

assert json.loads((OUT / "authorization.json").read_text())["authorized"] is False
identity = subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
assert subprocess.check_output([*SSH, "sha256sum " + shlex.quote(ELF)], text=True, timeout=10).split()[0] == ELF_SHA
subprocess.run([*SSH, "mkdir -p " + shlex.quote(BASE)], check=True, timeout=10)
manifest = {"status": "PREPARED_NOT_AUTHORIZED", "target": identity, "base": BASE,
            "installed_elf": {"path": ELF, "sha256": ELF_SHA}, "files": {}}
for name, source in FILES.items():
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                    str(source), f"robot-dev:{BASE}/{name}"], check=True, timeout=20)
    remote = subprocess.check_output([*SSH, "sha256sum " + shlex.quote(f"{BASE}/{name}")], text=True, timeout=10).split()[0]
    assert remote == digest
    manifest["files"][name] = digest
python_files = [name for name in FILES if name.endswith(".py")]
subprocess.run([*SSH, "chmod 0755 " + " ".join(shlex.quote(f"{BASE}/{name}") for name in python_files)], check=True, timeout=10)
subprocess.run([*SSH, "python3 -m py_compile " + " ".join(shlex.quote(f"{BASE}/{name}") for name in python_files)],
               check=True, timeout=10)
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("PASS: three-hour soak runner staged but not authorized; no CAN operation started")
