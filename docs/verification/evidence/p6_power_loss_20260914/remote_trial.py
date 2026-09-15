#!/usr/bin/env python3
"""Run one authorized drive-power loss/restoration trial on RK3588."""
import hashlib
import json
import os
import select
import signal
import subprocess
import sys
import time
from pathlib import Path

from trial_checks import parse_capture

BASE = Path("/tmp/robot-control-qualifications/power-loss-fc77b14ec242")
INSTALL = Path("/opt/robot-control/staging/userspace-inhibit-fc77b14ec242")
ELF = INSTALL / "robot-control-zlac-qualification"
HELPER = INSTALL / "robot-control-can-interface-inhibitor"
ELF_SHA = "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4"
HELPER_SHA = "c2425001cbe583e3bb1e3307d4145e95cafd722d150154ff727fe519a80b1a5b"
ARGS = ["--interface", "can0", "--external-loss-once", "--interface-inhibitor", str(HELPER)]
OUT = BASE / "physical_once"

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
assert hashlib.sha256(HELPER.read_bytes()).hexdigest() == HELPER_SHA
assert Path("/etc/machine-id").read_text().strip() == "6923ab3301fb4a8d816759b04ec6bf0a"
assert subprocess.check_output(["getcap", str(HELPER)], text=True).strip() == f"{HELPER} cap_net_admin=ep"
assert subprocess.check_output(["getcap", str(ELF)], text=True).strip() == ""
ready = json.loads((BASE / "operator_ready.json").read_text())
assert 0 <= time.time() - ready["wall_time"] <= 180 and ready["elf_sha256"] == ELF_SHA
os.kill(ready["pid"], 0)
for entry in Path("/proc").iterdir():
    if not entry.name.isdigit():
        continue
    try:
        name = (entry / "exe").resolve(strict=True).name
        assert name not in ("cansend", "cangen", "robot-control-zlac-qualification", "robot-control-canopen-commission"), (entry.name, name)
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        pass
pre = snapshot()
info = pre["linkinfo"]["info_data"]
assert "UP" in pre["flags"] and pre["mtu"] == 16
assert info["state"] == "ERROR-ACTIVE" and info["bittiming"]["bitrate"] == 500000
assert all(value == 0 for value in info.get("berr_counter", {}).values())
OUT.mkdir(exist_ok=False)
(OUT / "can_preflight.json").write_text(json.dumps(pre, indent=2) + "\n")

capture = child = None
target_seen = moving_seen = False
processed = 0
first_error_time = down_wall_time = None
power_cut_armed_wall_time = None
post_error_requests = 0
rc = 1
try:
    with (OUT / "rk3588_can.log").open("wb") as raw, (OUT / "capture.stderr").open("wb") as capture_error:
        capture = subprocess.Popen(
            ["stdbuf", "-oL", "candump", "-D", "-ta", "-e", "-n", "100000", "can0,0:0,#FFFFFFFF"],
            stdout=raw, stderr=capture_error)
        time.sleep(0.25)
        assert capture.poll() is None and not (OUT / "capture.stderr").read_text()
        (BASE / "capture_ready.json").write_text(json.dumps({"wall_time": time.time()}))
        print("CAPTURE_READY", flush=True)
        operator_deadline = time.monotonic() + 120
        while not (BASE / "operator_armed.json").exists():
            assert time.monotonic() < operator_deadline and capture.poll() is None
            time.sleep(0.05)
        fresh_marker("operator_armed.json", ready["pid"], 5)
        print("OPERATOR_ARMED", flush=True)
        assert select.select([sys.stdin], [], [], 10)[0] and sys.stdin.readline().strip() == "GO"
        (OUT / "attempt_started.txt").write_text(str(time.time()) + "\n")
        with (OUT / "executor.log").open("wb") as log:
            child = subprocess.Popen([str(ELF), *ARGS], stdout=log, stderr=subprocess.STDOUT)
            print("EXECUTOR_STARTED", flush=True)
            deadline = time.monotonic() + 45
            final_power_off_seen = False
            while not final_power_off_seen:
                records = parse_capture((OUT / "rk3588_can.log").read_text())
                for timestamp, ident, payload in records[processed:]:
                    if ident & 0x20000000:
                        if first_error_time is None:
                            first_error_time = timestamp
                        continue
                    if first_error_time is not None and ident in (0, 0x201, 0x601):
                        post_error_requests += 1
                    if ident == 0x601 and payload.hex() == "23ff600300000500":
                        assert not target_seen, "more than one nonzero target"
                        target_seen = True
                    if target_seen and ident == 0x181 and len(payload) == 8:
                        assert payload[4:6] == bytes(2), "left wheel moved"
                        moving_seen = moving_seen or int.from_bytes(payload[6:], "little", signed=True) > 0
                processed = len(records)
                if target_seen and moving_seen and power_cut_armed_wall_time is None:
                    power_cut_armed_wall_time = time.time()
                    temporary = OUT / "power_cut_armed.json.tmp"
                    temporary.write_text(json.dumps({"pid": child.pid, "elf_sha256": ELF_SHA, "wall_time": power_cut_armed_wall_time}))
                    temporary.replace(OUT / "power_cut_armed.json")
                    print("POWER_CUT_ARMED", flush=True)
                current = snapshot()
                if down_wall_time is None and "UP" not in current["flags"]:
                    down_wall_time = time.time()
                    (OUT / "interface_down.json").write_text(json.dumps({
                        "first_error_timestamp": first_error_time,
                        "power_cut_armed_wall_time": power_cut_armed_wall_time,
                        "down_observed_wall_time": down_wall_time}, indent=2) + "\n")
                    print(f"INTERFACE_DOWN wall_time={down_wall_time:.6f}", flush=True)
                if capture.poll() is not None:
                    assert down_wall_time is not None and (OUT / "capture.stderr").read_text().strip() == "can0: interface down"
                final_power_off_seen = (BASE / "operator_final_power_off.json").exists()
                assert time.monotonic() < deadline, "operator/executor deadline"
                time.sleep(0.01)
            power_cut = fresh_marker("operator_power_cut.json", ready["pid"], 60)
            fresh_marker("operator_power_restore.json", ready["pid"], 30)
            fresh_marker("operator_no_restart.json", ready["pid"], 15)
            fresh_marker("operator_final_power_off.json", ready["pid"], 5)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.send_signal(signal.SIGTERM)
                child.wait(timeout=3)
                raise AssertionError("executor did not finish after power restoration")
            (OUT / "executor.rc").write_text(str(child.returncode) + "\n")
        if down_wall_time is None and "UP" not in snapshot()["flags"]:
            down_wall_time = time.time()
        records = parse_capture((OUT / "rk3588_can.log").read_text())
        errors = [(timestamp, ident, payload) for timestamp, ident, payload in records if ident & 0x20000000]
        post_error = [(timestamp, ident, payload) for timestamp, ident, payload in records
                      if errors and timestamp > errors[0][0] and ident in (0, 0x201, 0x601)]
        assert target_seen and moving_seen and not post_error and post_error_requests == 0
        if errors:
            assert errors[0][0] >= power_cut["wall_time"] - 0.25, "CAN error preceded operator power cut"
        assert sum(ident == 0x601 and payload.hex() == "23ff600300000500" for _, ident, payload in records) == 1
        executor = (OUT / "executor.log").read_text()
        assert "qualification_expected_external_loss_absent" not in executor and "Permission denied" not in executor
        post = snapshot()
        if child.returncode == 0:
            assert "UP" in post["flags"] and not errors
            assert "external_loss=observed cleanup=verified" in executor
            assert "interface_inhibitor_released interface=can0 cleanup=verified" in executor
            outcome = "ZERO_FIRST_RECOVERY"
        else:
            assert "UP" not in post["flags"] and errors
            assert "interface_inhibitor_armed interface=can0" in executor
            outcome = "INHIBITED_INTERFACE_DOWN"
        rc = 0
finally:
    (BASE / "capture_ready.json").unlink(missing_ok=True)
    if child is not None and child.poll() is None:
        child.send_signal(signal.SIGTERM)
        try:
            child.wait(timeout=3)
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
    capture_message = (OUT / "capture.stderr").read_text().strip()
    capture_stopped = capture is None or capture.poll() is not None
    if capture_message and not ("UP" not in post["flags"] and capture_message == "can0: interface down"):
        rc = 1
    safe = child is None or child.poll() is not None
    result = {
        "pass": rc == 0,
        "outcome": locals().get("outcome", "FAILED"),
        "interface_down": "UP" not in post["flags"],
        "executor_rc": None if child is None else child.returncode,
        "executor_stopped": safe,
        "capture_stopped": capture_stopped,
        "raw_can_error": first_error_time is not None,
        "post_error_requests": post_error_requests,
        "final_drive_power_off": (BASE / "operator_final_power_off.json").exists(),
    }
    (OUT / "wrapper_result.json").write_text(json.dumps(result, indent=2) + "\n")
    if not safe or not capture_stopped:
        rc = 1
print("OUTCOME " + result["outcome"] if rc == 0 else "OUTCOME FAILED_POWER_OFF_REQUIRED", flush=True)
print(f"TRIAL_FINISHED rc={rc}", flush=True)
sys.exit(rc)
