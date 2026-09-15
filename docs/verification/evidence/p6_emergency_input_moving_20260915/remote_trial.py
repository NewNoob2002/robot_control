#!/usr/bin/env python3
"""Run one authorized moving X1 emergency-input trial on RK3588."""
import hashlib
import json
import os
import select
import signal
import subprocess
import sys
import time
from pathlib import Path

from trial_checks import parse_capture, validate_locked_state, validate_records

BASE = Path("/tmp/robot-control-qualifications/emergency-input-moving-bb4f44251672")
OUT = BASE / "physical_once"
ELF = Path("/opt/robot-control/staging/emergency-input-zero-v2-bb4f44251672/robot-control-zlac-qualification")
ELF_SHA = "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7"
MOTION_ARGS = ["--interface", "can0", "--target-once", "2:5", "--duration-ms", "3000"]
MANUAL_ARGS = ["--interface", "can0", "--manual-tpdo"]


def snapshot():
    """Read physical can0 state without application transmission."""
    return json.loads(subprocess.check_output(
        ["ip", "-j", "-details", "-statistics", "link", "show", "can0"], text=True, timeout=5))[0]


def fresh_marker(name, ready_pid, maximum_age):
    """Read one fresh operator marker owned by the active watcher."""
    marker = json.loads((BASE / name).read_text())
    assert marker["pid"] == ready_pid and 0 <= time.time() - marker["wall_time"] <= maximum_age, marker
    return marker


def capture_records():
    """Read all complete records currently flushed by candump."""
    return parse_capture((OUT / "rk3588_can.log").read_text())


authorization = json.loads((BASE / "authorization.json").read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
assert authorization["motion_args"] == MOTION_ARGS and authorization["manual_args"] == MANUAL_ARGS
assert hashlib.sha256(ELF.read_bytes()).hexdigest() == ELF_SHA
assert subprocess.check_output(["stat", "-c", "%U %G %a", str(ELF)], text=True).strip() == "root cat 750"
assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
ready = json.loads((BASE / "operator_ready.json").read_text())
assert 0 <= time.time() - ready["wall_time"] <= 180 and ready["elf_sha256"] == ELF_SHA
os.kill(ready["pid"], 0)
for entry in Path("/proc").iterdir():
    if not entry.name.isdigit():
        continue
    try:
        name = (entry / "exe").resolve(strict=True).name
        assert name not in ("cansend", "cangen", "robot-control-zlac-qualification",
                            "robot-control-canopen-commission"), name
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        pass
pre = snapshot()
info = pre["linkinfo"]["info_data"]
assert "UP" in pre["flags"] and pre["mtu"] == 16 and info["state"] == "ERROR-ACTIVE"
assert info["bittiming"]["bitrate"] == 500000 and all(value == 0 for value in info.get("berr_counter", {}).values())
OUT.mkdir(exist_ok=False)
(OUT / "can_preflight.json").write_text(json.dumps(pre, indent=2) + "\n")
capture = motion = manual = None
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
        (OUT / "armed_ready.json").write_text(json.dumps({"wall_time": time.time()}) + "\n")
        print("ARMED_READY", flush=True)
        deadline = time.monotonic() + 180
        while not (BASE / "operator_armed.json").exists():
            assert time.monotonic() < deadline, "operator ARMED deadline"
            time.sleep(0.05)
        fresh_marker("operator_armed.json", ready["pid"], 10)

        with (OUT / "motion_executor.log").open("wb") as log:
            motion = subprocess.Popen([str(ELF), *MOTION_ARGS], stdout=log, stderr=subprocess.STDOUT)
            print("MOTION_EXECUTOR_STARTED", flush=True)
            press_prompted = False
            while not (BASE / "operator_estop_locked.json").exists():
                assert motion.poll() is None, "motion ended before X1 was locked"
                for _, ident, payload in capture_records():
                    if ident == 0x181 and len(payload) == 8:
                        left = int.from_bytes(payload[4:6], "little", signed=True)
                        right = int.from_bytes(payload[6:8], "little", signed=True)
                        assert left == 0, "left-wheel feedback became nonzero"
                        if right != 0 and not press_prompted:
                            (OUT / "press_x1_now.json").write_text(json.dumps({"wall_time": time.time()}) + "\n")
                            print("PRESS_X1_NOW", flush=True)
                            press_prompted = True
                assert time.monotonic() < deadline and capture.poll() is None
                time.sleep(0.02)
            assert press_prompted
            fresh_marker("operator_estop_locked.json", ready["pid"], 5)
            motion.wait(timeout=max(1, deadline - time.monotonic()))
            (OUT / "motion_executor.rc").write_text(str(motion.returncode) + "\n")
        assert motion.returncode == 0
        motion_log = (OUT / "motion_executor.log").read_text()
        assert "qualification_complete node=1 operation=4" in motion_log

        manual_started_at = time.time()
        (OUT / "manual_started.json").write_text(json.dumps({"wall_time": manual_started_at}) + "\n")
        with (OUT / "manual_executor.log").open("wb") as log:
            manual = subprocess.Popen([str(ELF), *MANUAL_ARGS], stdout=log, stderr=subprocess.STDOUT)
            print("MANUAL_EXECUTOR_STARTED", flush=True)
            while "MANUAL_ROTATION_READY" not in (OUT / "manual_executor.log").read_text():
                assert manual.poll() is None and time.monotonic() < deadline, "manual TPDO readiness failed"
                time.sleep(0.05)
            locked = validate_locked_state(capture_records(), manual_started_at)
            (OUT / "safe_to_reset_x1.json").write_text(json.dumps({"wall_time": time.time(), **locked}, indent=2) + "\n")
            print("SAFE_TO_RESET_X1", flush=True)
            while not (BASE / "operator_no_restart.json").exists():
                assert manual.poll() is None and time.monotonic() < deadline, "operator reset deadline"
                time.sleep(0.05)
            fresh_marker("operator_estop_reset.json", ready["pid"], 15)
            fresh_marker("operator_no_restart.json", ready["pid"], 10)
            manual.wait(timeout=max(1, deadline - time.monotonic()))
            (OUT / "manual_executor.rc").write_text(str(manual.returncode) + "\n")
        assert manual.returncode == 0
        manual_log = (OUT / "manual_executor.log").read_text()
        assert "MANUAL_ROTATION_READY" in manual_log and "MANUAL_ROTATION_END" in manual_log
        checks = validate_records(capture_records())
        rc = 0
        result = {"pass": True, **checks, "motion_executor_rc": 0, "manual_executor_rc": 0,
                  "safe_state_requires_drive_power_off": True}
finally:
    for child in (motion, manual):
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
    result |= {"pass": rc == 0, "motion_executor_stopped": motion is None or motion.poll() is not None,
               "manual_executor_stopped": manual is None or manual.poll() is not None,
               "capture_stopped": capture is None or capture.poll() is not None}
    (OUT / "wrapper_result.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"TRIAL_FINISHED rc={rc}", flush=True)
sys.exit(rc)
