#!/usr/bin/env python3
"""Run one authorized zero-motion X1 emergency-input trial on RK3588."""
import hashlib
import json
import os
import re
import select
import signal
import subprocess
import sys
import time
from pathlib import Path

from trial_checks import parse_capture, validate_records

BASE = Path("/tmp/robot-control-qualifications/emergency-input-zero-fc77b14ec242")
OUT = BASE / "physical_once"
ELF = Path("/opt/robot-control/staging/userspace-inhibit-fc77b14ec242/robot-control-zlac-qualification")
ELF_SHA = "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"
ARGS = ["--interface", "can0", "--manual-tpdo"]


def snapshot():
    """Read physical can0 state without application transmission."""
    return json.loads(subprocess.check_output(
        ["ip", "-j", "-details", "-statistics", "link", "show", "can0"], text=True, timeout=5))[0]


def fresh_marker(name, ready_pid, maximum_age):
    """Read one fresh operator marker owned by the active watcher."""
    marker = json.loads((BASE / name).read_text())
    assert marker["pid"] == ready_pid and 0 <= time.time() - marker["wall_time"] <= maximum_age, marker
    return marker


authorization = json.loads((BASE / "authorization.json").read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
assert hashlib.sha256(ELF.read_bytes()).hexdigest() == ELF_SHA
assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
ready = json.loads((BASE / "operator_ready.json").read_text())
assert 0 <= time.time() - ready["wall_time"] <= 180 and ready["elf_sha256"] == ELF_SHA
os.kill(ready["pid"], 0)
for entry in Path("/proc").iterdir():
    if not entry.name.isdigit():
        continue
    try:
        name = (entry / "exe").resolve(strict=True).name
        assert name not in ("cansend", "cangen", "robot-control-zlac-qualification", "robot-control-canopen-commission"), name
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        pass
pre = snapshot()
info = pre["linkinfo"]["info_data"]
assert "UP" in pre["flags"] and pre["mtu"] == 16 and info["state"] == "ERROR-ACTIVE"
assert info["bittiming"]["bitrate"] == 500000 and all(value == 0 for value in info.get("berr_counter", {}).values())
OUT.mkdir(exist_ok=False)
(OUT / "can_preflight.json").write_text(json.dumps(pre, indent=2) + "\n")
capture = child = None
rc = 1
result = {}
try:
    with (OUT / "rk3588_can.log").open("wb") as raw, (OUT / "capture.stderr").open("wb") as capture_error:
        capture = subprocess.Popen(
            ["stdbuf", "-oL", "candump", "-D", "-ta", "-e", "-n", "100000", "can0,0:0,#FFFFFFFF"],
            stdout=raw, stderr=capture_error)
        time.sleep(0.25)
        assert capture.poll() is None and not (OUT / "capture.stderr").read_text()
        print("CAPTURE_READY", flush=True)
        assert select.select([sys.stdin], [], [], 10)[0] and sys.stdin.readline().strip() == "GO"
        (OUT / "attempt_started.txt").write_text(str(time.time()) + "\n")
        with (OUT / "executor.log").open("wb") as log:
            child = subprocess.Popen([str(ELF), *ARGS], stdout=log, stderr=subprocess.STDOUT)
            print("EXECUTOR_STARTED", flush=True)
            deadline = time.monotonic() + 80
            while "MANUAL_ROTATION_READY" not in (OUT / "executor.log").read_text():
                assert child.poll() is None and time.monotonic() < deadline, "manual TPDO readiness failed"
                time.sleep(0.05)
            (OUT / "estop_ready.json").write_text(json.dumps({"wall_time": time.time()}))
            print("ESTOP_READY", flush=True)
            while not (BASE / "operator_no_motion.json").exists():
                assert child.poll() is None and time.monotonic() < deadline, "operator action deadline"
                time.sleep(0.05)
            fresh_marker("operator_estop_locked.json", ready["pid"], 30)
            fresh_marker("operator_estop_reset.json", ready["pid"], 20)
            fresh_marker("operator_no_motion.json", ready["pid"], 10)
            child.wait(timeout=max(1, deadline - time.monotonic()))
            (OUT / "executor.rc").write_text(str(child.returncode) + "\n")
        assert child.returncode == 0
        executor = (OUT / "executor.log").read_text()
        assert "MANUAL_ROTATION_READY" in executor and "MANUAL_ROTATION_END" in executor
        assert all(int(value) == 0 for value in re.findall(r"(?:tpdo_velocity_raw|manual_velocity sub=\d raw)=(\d+)", executor))
        checks = validate_records(parse_capture((OUT / "rk3588_can.log").read_text()))
        rc = 0
        result = {"pass": True, **checks, "executor_rc": 0, "no_motor_commands": True,
                  "safe_state_requires_drive_power_off": True}
finally:
    if child is not None and child.poll() is None:
        child.send_signal(signal.SIGTERM)
        try:
            child.wait(timeout=7)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=2)
    if capture is not None and capture.poll() is None:
        capture.send_signal(signal.SIGINT)
        try:
            capture.wait(timeout=2)
        except subprocess.TimeoutExpired:
            capture.kill()
            capture.wait(timeout=2)
    post = snapshot()
    (OUT / "can_postflight.json").write_text(json.dumps(post, indent=2) + "\n")
    result |= {"pass": rc == 0, "executor_stopped": child is None or child.poll() is not None,
               "capture_stopped": capture is None or capture.poll() is not None}
    (OUT / "wrapper_result.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"TRIAL_FINISHED rc={rc}", flush=True)
sys.exit(rc)
