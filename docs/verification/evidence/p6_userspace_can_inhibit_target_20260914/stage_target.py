#!/usr/bin/env python3
"""Stage hash-checked aarch64 artifacts and run non-actuating target checks."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
BASE = "/tmp/robot-control-qualifications/userspace-inhibit-fc77b14ec242"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
FILES = {
    "robot-control-zlac-qualification": (
        ROOT / "out/build/cross/userspace-inhibit-container-qualification/tools/zlac_qualification/robot-control-zlac-qualification",
        "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4",
    ),
    "robot-control-can-interface-inhibitor": (
        ROOT / "out/build/cross/userspace-inhibit-container-qualification/tools/can_interface_inhibitor/robot-control-can-interface-inhibitor",
        "c2425001cbe583e3bb1e3307d4145e95cafd722d150154ff727fe519a80b1a5b",
    ),
    "target_vcan_smoke.sh": (OUT / "target_vcan_smoke.sh", None),
}

identity = subprocess.check_output([*SSH, "cat /etc/machine-id"], text=True, timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
subprocess.run([*SSH, "mkdir -p " + shlex.quote(BASE)], check=True, timeout=10)
manifest = {"target": identity, "base": BASE, "files": {}}
for name, (source, expected) in FILES.items():
    actual = hashlib.sha256(source.read_bytes()).hexdigest()
    assert expected is None or actual == expected
    subprocess.run(
        ["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", str(source), f"robot-dev:{BASE}/{name}"],
        check=True,
        timeout=20,
    )
    remote = subprocess.check_output([*SSH, "sha256sum " + shlex.quote(f"{BASE}/{name}")], text=True, timeout=10).split()[0]
    assert remote == actual
    manifest["files"][name] = actual
subprocess.run([*SSH, "chmod 0755 " + " ".join(shlex.quote(f"{BASE}/{name}") for name in FILES)], check=True, timeout=10)
(OUT / "deployment_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
audit = subprocess.run(
    [*SSH, f"file {shlex.quote(BASE)}/*; ldd {shlex.quote(BASE)}/robot-control-zlac-qualification; ldd {shlex.quote(BASE)}/robot-control-can-interface-inhibitor"],
    capture_output=True,
    text=True,
    timeout=20,
)
(OUT / "target_elf_audit.log").write_text(audit.stdout)
(OUT / "target_elf_audit.stderr").write_text(audit.stderr)
assert audit.returncode == 0
smoke = subprocess.run(
    [*SSH, f"{shlex.quote(BASE)}/target_vcan_smoke.sh {shlex.quote(BASE)}/robot-control-zlac-qualification {shlex.quote(BASE)}/robot-control-can-interface-inhibitor"],
    capture_output=True,
    text=True,
    timeout=30,
)
(OUT / "target_vcan_smoke.log").write_text(smoke.stdout)
(OUT / "target_vcan_smoke.stderr").write_text(smoke.stderr)
assert smoke.returncode == 0 and "PASS target isolated vcan" in smoke.stdout, smoke.stdout + smoke.stderr
print("PASS staged target artifacts and isolated non-actuating checks")
