#!/usr/bin/env python3
"""Coordinate one moving X1 trial with a silent JCAN observer."""
import json
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

from trial_checks import ALLOWED_REQUESTS, parse_capture, validate_records

OUT = Path(__file__).resolve().parent
JCAN = "/home/gtc/Desktop/workspace/JCAN/target/release/jcan"
SERIAL = "207F346D5650"
BASE = "/tmp/robot-control-qualifications/emergency-input-moving-bb4f44251672"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
authorization = json.loads((OUT / "authorization.json").read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
(OUT / "run_once.marker").touch(exist_ok=False)
events = queue.Queue()
threads = []
remote = observer = None
baseline = None
error = None
started = manual_started = False
jcan_records = []
capture_sequences_match = False


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


def append_frame(received_at, data):
    """Retain one passive frame and reject immediate envelope violations."""
    assert len(jcan_records) < 100000
    assert not any(data.get(key, False) for key in ("extended", "fd", "brs", "remote")), data
    ident = data["can_id"]
    payload = bytes.fromhex(data["data_hex"])
    assert not ident & 0x20000000, "JCAN observed CAN error"
    assert ident in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701), hex(ident)
    if ident == 0x601:
        assert payload in ALLOWED_REQUESTS, payload.hex()
    elif ident == 0x581:
        assert len(payload) == 8 and payload[0] != 0x80, payload.hex()
    jcan_records.append((received_at, ident, payload))


try:
    assert jcan_read(["self-test"], "jcan_self_test.json")["passed"]
    assert any(device["serial"] == SERIAL for device in jcan_read(["scan"], "jcan_scan.json"))
    baseline = jcan_read(["--serial", SERIAL, "config-get"], "jcan_config_pre.json")
    assert baseline["can_speed"] == "0C" and baseline["standard"] == "00" and baseline["term_res"] == "00"
    observer = subprocess.Popen([JCAN, "--json", "--serial", SERIAL, "session", "--mode", "silent", "--receive"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    attach(observer, "jcan")
    deadline = time.monotonic() + 210
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
                append_frame(received_at, data)
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
            elif message == "MOTION_EXECUTOR_STARTED":
                started = True
            elif message == "MANUAL_EXECUTOR_STARTED":
                manual_started = True
            elif message in ("ARMED_READY", "PRESS_X1_NOW", "SAFE_TO_RESET_X1"):
                pass
            elif not message.startswith("TRIAL_FINISHED rc=0"):
                raise RuntimeError(message)
        elif label == "jcan_out_eof":
            raise RuntimeError("JCAN disconnected")
    checks = validate_records(jcan_records)
    assert started and manual_started
    wait = subprocess.run([*SSH, "sh -c 'for i in $(seq 1 1200); do test -f " + BASE
                           + "/physical_once/operator_observation.json && exit 0; sleep .1; done; exit 1'"], timeout=125)
    assert wait.returncode == 0, "operator final power-off observation missing"
except Exception as exc:
    error = str(exc)
    print("STOP: " + error, flush=True)
finally:
    if remote is not None and remote.poll() is None:
        try:
            remote.wait(timeout=210)
        except subprocess.TimeoutExpired:
            error = (error or "") + " remote safe-cleanup deadline"
            remote.kill()
            remote.wait(timeout=2)
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
        else:
            try:
                target_records = parse_capture((OUT / "target/rk3588_can.log").read_text())
                capture_sequences_match = [(ident, payload) for _, ident, payload in target_records] == [
                    (ident, payload) for _, ident, payload in jcan_records]
                assert capture_sequences_match, "target and JCAN capture sequences differ"
            except Exception as exc:
                error = (error or "") + " capture comparison failed: " + str(exc)
    if baseline is not None:
        try:
            assert jcan_read(["--serial", SERIAL, "config-get"], "jcan_config_post.json") == baseline
        except Exception as exc:
            error = (error or "") + " JCAN postflight failed: " + str(exc)
    coordinator = {"error": error, "motion_started": started, "manual_started": manual_started,
                   "jcan_mode": "silent", "jcan_data_frame_commands": 0, "jcan_frames": len(jcan_records),
                   "target_and_jcan_exact_sequence_match": capture_sequences_match}
    if "checks" in locals():
        coordinator.update(checks)
    (OUT / "coordinator_result.json").write_text(json.dumps(coordinator, indent=2) + "\n")
print("COORDINATOR_FINISHED error=" + str(error), flush=True)
sys.exit(1 if error else 0)
