#!/usr/bin/env python3
"""Coordinate one zero-motion X1 trial with a silent JCAN observer."""
import json
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

from trial_checks import validate_records

OUT = Path(__file__).resolve().parent
JCAN = "/home/gtc/Desktop/workspace/JCAN/target/release/jcan"
SERIAL = "207F346D5650"
BASE = "/tmp/robot-control-qualifications/emergency-input-zero-v2-bb4f44251672"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
authorization = json.loads((OUT / "authorization.json").read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
(OUT / "run_once.marker").touch(exist_ok=False)
events = queue.Queue()
threads = []
remote = observer = None
baseline = None
error = None
started = manual_ready = False
jcan_records = []


def read_lines(stream, label, path):
    """Archive a process stream and publish complete lines."""
    with path.open("wb") as log:
        for line in iter(stream.readline, b""):
            log.write(line)
            log.flush()
            events.put((label, line, time.monotonic()))
    events.put((label + "_eof", b"", time.monotonic()))


def attach(process, prefix):
    """Start stream readers for one owned child process."""
    for stream, suffix in ((process.stdout, "out"), (process.stderr, "err")):
        label = prefix + "_" + suffix
        path = OUT / ("jcan_session.jsonl" if label == "jcan_out" else label + ".log")
        thread = threading.Thread(target=read_lines, args=(stream, label, path), daemon=True)
        threads.append(thread)
        thread.start()


def jcan_read(arguments, filename):
    """Run and archive one read-only JCAN command."""
    result = subprocess.run([JCAN, "--json", *arguments], capture_output=True, timeout=10)
    (OUT / filename).write_bytes(result.stdout)
    (OUT / (filename + ".stderr")).write_bytes(result.stderr)
    assert result.returncode == 0 and not result.stderr
    data = json.loads(result.stdout)
    assert data.get("ok") is True and not data.get("warnings"), data
    return data["data"]


try:
    assert jcan_read(["self-test"], "jcan_self_test.json")["passed"]
    assert any(device["serial"] == SERIAL for device in jcan_read(["scan"], "jcan_scan.json"))
    baseline = jcan_read(["--serial", SERIAL, "config-get"], "jcan_config_pre.json")
    assert baseline["can_speed"] == "0C" and baseline["standard"] == "00" and baseline["term_res"] == "00"
    observer = subprocess.Popen([JCAN, "--json", "--serial", SERIAL, "session", "--mode", "silent", "--receive"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    attach(observer, "jcan")
    deadline = time.monotonic() + 180
    connected = receive_enabled = False
    while True:
        assert time.monotonic() < deadline and observer.poll() is None
        try:
            label, line, received_at = events.get(timeout=0.05)
        except queue.Empty:
            if remote is not None and remote.poll() is not None:
                assert remote.returncode == 0
                break
            continue
        if label in ("jcan_err", "remote_err"):
            raise RuntimeError(label + ": " + line.decode(errors="replace"))
        if label == "jcan_out":
            data = json.loads(line)
            assert data.get("ok") is not False and not data.get("warnings") and not data.get("error"), data
            if data.get("event") == "connected":
                connected = data["serial"] == SERIAL and data["mode"] == "silent"
            elif data.get("event") == "session_started":
                receive_enabled = data["receive_enabled"] is True
            elif data.get("event") == "frame":
                jcan_records.append((received_at, data["can_id"], bytes.fromhex(data["data_hex"])))
            else:
                raise RuntimeError("unexpected JCAN event: " + str(data))
            if connected and receive_enabled and remote is None:
                remote = subprocess.Popen([*SSH, "python3", "-u", BASE + "/remote_trial.py"],
                                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                attach(remote, "remote")
                print("JCAN_READY: silent receive observer connected; no ACK or data TX", flush=True)
        elif label == "remote_out":
            message = line.decode().strip()
            print(message, flush=True)
            if message == "CAPTURE_READY":
                remote.stdin.write(b"GO\n")
                remote.stdin.flush()
            elif message == "EXECUTOR_STARTED":
                started = True
            elif message == "ESTOP_READY":
                manual_ready = True
            elif not message.startswith("TRIAL_FINISHED rc=0"):
                raise RuntimeError(message)
        elif label == "jcan_out_eof":
            raise RuntimeError("JCAN disconnected")
    checks = validate_records(jcan_records)
    assert started and manual_ready
    wait = subprocess.run([*SSH, "sh -c 'for i in $(seq 1 1200); do test -f " + BASE
                           + "/physical_once/operator_observation.json && exit 0; sleep .1; done; exit 1'"], timeout=125)
    assert wait.returncode == 0, "operator final power-off observation missing"
except Exception as exc:
    error = str(exc)
    print("STOP: " + error, flush=True)
finally:
    if remote is not None and remote.poll() is None:
        remote.terminate()
        try:
            remote.wait(timeout=9)
        except subprocess.TimeoutExpired:
            remote.kill()
            remote.wait(timeout=2)
            error = (error or "") + " remote cleanup timeout"
    if observer is not None and observer.poll() is None:
        try:
            request = json.dumps({"id": 99, "op": "shutdown"}) + "\n"
            (OUT / "jcan_requests.jsonl").write_text(request)
            observer.stdin.write(request.encode())
            observer.stdin.flush()
            observer.wait(timeout=5)
        except (BrokenPipeError, subprocess.TimeoutExpired) as exc:
            error = (error or "") + " JCAN cleanup error: " + str(exc)
        if observer.poll() is None:
            observer.terminate()
            observer.wait(timeout=3)
    for thread in threads:
        thread.join(timeout=2)
    if remote is not None:
        transfer = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "-r",
                                   "robot-dev:" + BASE + "/physical_once", str(OUT / "target")], capture_output=True, timeout=20)
        (OUT / "transfer.stderr").write_bytes(transfer.stderr)
        if transfer.returncode != 0:
            error = (error or "") + " target evidence transfer failed"
    if baseline is not None:
        try:
            assert jcan_read(["--serial", SERIAL, "config-get"], "jcan_config_post.json") == baseline
        except Exception as exc:
            error = (error or "") + " JCAN postflight failed: " + str(exc)
    (OUT / "coordinator_result.json").write_text(json.dumps({"error": error, "started": started,
        "manual_ready": manual_ready, "jcan_mode": "silent", "jcan_data_frame_commands": 0,
        "jcan_frames": len(jcan_records)}, indent=2) + "\n")
print("COORDINATOR_FINISHED error=" + str(error), flush=True)
sys.exit(1 if error else 0)
