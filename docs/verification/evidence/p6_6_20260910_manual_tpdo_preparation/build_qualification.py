#!/usr/bin/env python3
"""Build and audit the communication-loss Debug artifact from an attested source snapshot."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
evidence = Path(__file__).resolve().parent
sysroot = root / "sysroots/rk3588-ubuntu2204"
lock = root / "sysroots/locks/rk3588-ubuntu2204-a685ab13.json"
image = "sha256:f2198e31e27c084bc2deff761e124fa9d7ce580a8d986c7885fd62bb1701e7dd"
build = root / "out/build/cross/p66-manual-tpdo-debug"
if build.exists():
    raise SystemExit("Build directory already exists; preserve evidence and select a new preparation run.")
subprocess.run([root / "scripts/build/verify_canopen_dependencies.sh"], check=True)
subprocess.run([root / "scripts/sysroot/validate_sysroot.sh", sysroot, lock], check=True)
subprocess.run([root / "scripts/build/verify_cross_image.sh", image, root], check=True)
inputs = root / "out/build-inputs"
inputs.mkdir(parents=True, exist_ok=True)
snapshot = Path(tempfile.mkdtemp(prefix="p66-manual-tpdo-", dir=inputs)) / "source"
attestation = evidence / "source_attestation.json"
subprocess.run([root / "scripts/build/create_source_snapshot.sh", snapshot, attestation], check=True)
source_files = ["communication/canopen/qualification.cpp", "communication/canopen/qualification.hpp",
                "communication/canopen/qualification_gate.c", "communication/canopen/qualification_gate.h",
                "tools/zlac_qualification/main.cpp", "tools/zlac_qualification/CMakeLists.txt",
                "tests/unit/canopen_qualification_gate_tests.cpp", "tests/unit/canopen_qualification_vcan_tests.cpp",
                "tests/unit/CMakeLists.txt"]
source_hashes = {name: hashlib.sha256((snapshot / name).read_bytes()).hexdigest() for name in source_files}
base = ["docker", "run", "--rm", "--user", f"{os.getuid()}:{os.getgid()}", "--read-only",
        "--tmpfs", "/tmp:rw,nosuid,nodev,noexec", "--network", "none", "--cap-drop", "ALL",
        "--security-opt", "no-new-privileges", "--env", "HOME=/tmp/robot-control-home",
        "--env", "ROBOT_CONTROL_SYSROOT=/opt/robot-control/sysroot",
        "--mount", f"type=bind,source={snapshot},target=/workspace,readonly",
        "--mount", f"type=bind,source={root / 'out'},target=/workspace/out",
        "--mount", f"type=bind,source={sysroot},target=/opt/robot-control/sysroot,readonly",
        "--workdir", "/workspace", image]
configure = ["cmake", "-S", "/workspace", "-B", "/workspace/out/build/cross/p66-manual-tpdo-debug",
             "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", "-DBUILD_TESTING=OFF",
             "-DCMAKE_TOOLCHAIN_FILE=/workspace/cmake/toolchains/aarch64-rk3588-ubuntu2204.cmake",
             "-DROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION=ON", "-DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON"]
commands = [base + configure,
            base + ["cmake", "--build", "/workspace/out/build/cross/p66-manual-tpdo-debug", "-j", "8"],
            base + ["bash", "scripts/build/audit_qualification_elf.sh",
                    "/workspace/out/build/cross/p66-manual-tpdo-debug/tools/zlac_qualification/robot-control-zlac-qualification",
                    "/opt/robot-control/sysroot"]]
(evidence / "cross_commands.json").write_text(json.dumps(commands, indent=2) + chr(10))
for command in commands:
    subprocess.run(command, check=True)
subprocess.run([root / "scripts/sysroot/validate_sysroot.sh", sysroot, lock], check=True)
elf = build / "tools/zlac_qualification/robot-control-zlac-qualification"
metadata = {"elf": str(elf.relative_to(root)), "sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
            "source_snapshot": str(snapshot.relative_to(root)), "image": image,
            "source_hashes": source_hashes, "deployed": False, "physical_tests": "pending"}
(evidence / "qualification_artifact.json").write_text(json.dumps(metadata, indent=2) + chr(10))
print(json.dumps(metadata, indent=2))
