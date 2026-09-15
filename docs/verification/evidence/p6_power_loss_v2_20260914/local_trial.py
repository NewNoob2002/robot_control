#!/usr/bin/env python3
"""Coordinate one power-loss trial with a passive JCAN observer."""
import json
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

OUT = Path(__file__).resolve().parent
JCAN = "/home/gtc/Desktop/workspace/JCAN/target/release/jcan"
SERIAL = "207F346D5650"
BASE = "/tmp/robot-control-qualifications/power-loss-v2-fc77b14ec242"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
authorization = json.loads((OUT / "authorization.json").read_text())
assert authorization["authorized"] is True and authorization["attempts"] == 1
(OUT / "run_once.marker").touch(exist_ok=False)
events = queue.Queue()
threads = []
remote = observer = None
error = None
go = started = connected = receive_enabled = False
baseline = None
frames = downloads = uploads = nmts = nonzero = 0
first_error_at = post_motion_boot_at = None
host_after_error = 0
first_host_after_boot = None
remote_outcome = None

def read_lines(stream, label, path):
    """Archive a process stream and publish complete lines."""
    with path.open("wb") as log:
        for line in iter(stream.readline, b""):
            log.write(line)
            log.flush()
            events.put((label, line, time.monotonic()))
    events.put((label + "_eof", b"", time.monotonic()))

def attach(process, prefix):
    """Start bounded-lifetime readers for one owned child process."""
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
    assert result.returncode == 0 and not result.stderr, result.stderr.decode()
    data = json.loads(result.stdout)
    assert data.get("ok") is True and not data.get("warnings"), data
    return data["data"]

def validate_frame(frame, received_at):
    """Reject traffic outside the exact bounded qualification set."""
    global frames, downloads, uploads, nmts, nonzero
    global first_error_at, post_motion_boot_at, host_after_error, first_host_after_boot
    frames += 1
    assert frames <= 100000, "capture frame bound"
    assert not any(frame.get(key, False) for key in ("extended", "fd", "brs", "remote")), frame
    ident = frame["can_id"]
    payload = bytes.fromhex(frame["data_hex"])
    if ident & 0x20000000:
        if first_error_at is None:
            first_error_at = received_at
        return
    assert ident in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701), frame
    if first_error_at is not None and received_at > first_error_at and ident in (0, 0x201, 0x601):
        host_after_error += 1
    if nonzero and ident == 0x701 and payload == bytes([0]):
        post_motion_boot_at = post_motion_boot_at or received_at
        return
    if post_motion_boot_at is not None and received_at > post_motion_boot_at and ident in (0, 0x201, 0x601) and first_host_after_boot is None:
        first_host_after_boot = (ident, payload.hex())
    if ident in (0x281, 0x381, 0x481):
        assert len(payload) == 0, frame
    if ident == 0:
        assert go and payload in (bytes([1, 1]), bytes([0x80, 1])), frame
        assert nonzero == 0 or payload == bytes([0x80, 1]), "Operational after motion"
        nmts += 1
        assert nmts <= 6
        return
    if ident == 0x581:
        assert len(payload) == 8 and payload[0] != 0x80, frame
        return
    if ident != 0x601:
        return
    assert go and len(payload) == 8, "request before GO or malformed request"
    command = payload[0]
    index = int.from_bytes(payload[1:3], "little")
    sub = payload[3]
    if command == 0x40:
        allowed = {(0x6060, 0), (0x6061, 0), (0x1017, 0), (0x603F, 0), (0x6041, 0),
                   (0x200F, 0), (0x2000, 0), (0x605A, 0), (0x60FF, 1), (0x60FF, 2), (0x60FF, 3)}
        allowed |= {(0x606C, item) for item in (1, 2, 3)}
        allowed |= {(0x1800, item) for item in (1, 2, 5)} | {(0x1A00, item) for item in (0, 1, 2)}
        assert (index, sub) in allowed and payload[4:] == bytes(4), frame
        uploads += 1
        assert uploads <= 512
        return
    value = int.from_bytes(payload[4:], "little")
    allowed = {(0x2B, 0x1017, 0, 0), (0x2B, 0x1017, 0, 500),
               (0x2F, 0x6060, 0, 3), (0x2B, 0x6040, 0, 0),
               (0x2B, 0x6040, 0, 6), (0x2B, 0x6040, 0, 7), (0x2B, 0x6040, 0, 15),
               (0x23, 0x60FF, 3, 0), (0x23, 0x60FF, 3, 327680),
               (0x2B, 0x2000, 0, 0), (0x2B, 0x2000, 0, 1000)}
    assert (command, index, sub, value) in allowed, frame
    downloads += 1
    assert downloads <= 24
    if index == 0x60FF and value != 0:
        nonzero += 1
        assert nonzero == 1
    if nonzero and index == 0x6040 and value in (7, 15):
        raise AssertionError("activation after motion")

try:
    assert jcan_read(["self-test"], "jcan_self_test.json")["passed"]
    scan = jcan_read(["scan"], "jcan_scan.json")
    assert any(device["serial"] == SERIAL for device in scan)
    baseline = jcan_read(["--serial", SERIAL, "config-get"], "jcan_config_pre.json")
    assert baseline["can_speed"] == "0C" and baseline["standard"] == "00" and baseline["term_res"] == "00", baseline
    observer = subprocess.Popen([JCAN, "--json", "--serial", SERIAL, "session", "--mode", "normal", "--receive"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    attach(observer, "jcan")
    deadline = time.monotonic() + 240
    while True:
        assert time.monotonic() < deadline, "bounded capture deadline"
        assert observer.poll() is None, "JCAN session exited unexpectedly"
        try:
            label, line, received_at = events.get(timeout=0.02)
        except queue.Empty:
            if remote is not None and remote.poll() is not None:
                assert remote.returncode == 0, "remote trial failed"
                break
            continue
        if label in ("jcan_err", "remote_err"):
            raise RuntimeError(label + ": " + line.decode(errors="replace"))
        if label == "jcan_out":
            data = json.loads(line)
            assert data.get("ok") is not False and not data.get("warnings") and not data.get("error"), data
            event = data.get("event")
            if event == "session_started":
                assert data["serial"] == SERIAL and data["receive_enabled"] is True
                receive_enabled = True
            elif event == "connected":
                assert not connected and data["serial"] == SERIAL and data["mode"] == "normal", data
                connected = True
            elif event == "frame":
                validate_frame(data, received_at)
            else:
                raise RuntimeError("unexpected session event: " + str(data))
            if connected and receive_enabled and remote is None:
                remote = subprocess.Popen([*SSH, "python3", "-u", BASE + "/remote_trial.py"],
                                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                attach(remote, "remote")
                print("JCAN_READY: normal receive observer connected; ACK active, data TX disabled", flush=True)
        elif label == "remote_out":
            message = line.decode().strip()
            print(message, flush=True)
            if message == "CAPTURE_READY":
                assert connected and receive_enabled and not go
            elif message == "OPERATOR_ARMED":
                assert connected and receive_enabled and not go
                go = True
                remote.stdin.write(b"GO\n")
                remote.stdin.flush()
            elif message == "EXECUTOR_STARTED":
                started = True
            elif message == "POWER_CUT_ARMED":
                pass
            elif message == "OUTCOME ZERO_FIRST_RECOVERY":
                remote_outcome = message.removeprefix("OUTCOME ")
            elif message.startswith("INTERFACE_DOWN " ) or message.startswith("TRIAL_FINISHED rc=0"):
                pass
            else:
                raise RuntimeError(message)
        elif label == "jcan_out_eof":
            raise RuntimeError("JCAN disconnected")
    assert started and nonzero == 1 and post_motion_boot_at is not None and remote_outcome is not None
    assert host_after_error == 0
    assert remote_outcome == "ZERO_FIRST_RECOVERY"
    assert first_host_after_boot == (0x601, "23ff600300000000"), first_host_after_boot
except Exception as exc:
    error = str(exc)
    print("STOP: " + error, flush=True)
    if go:
        print("Operator: use emergency stop if needed and leave drive power OFF.", flush=True)
finally:
    if remote is not None and remote.poll() is None:
        remote.stdin.close()
        try:
            remote.wait(timeout=9)
        except subprocess.TimeoutExpired:
            remote.terminate()
            error = (error or "") + " remote cleanup unavailable; drive power must remain off"
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
        if observer.returncode != 0:
            error = (error or "") + " JCAN nonzero exit"
    for thread in threads:
        thread.join(timeout=2)
    if remote is not None:
        transfer = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "-r",
                                   "robot-dev:" + BASE + "/physical_once", str(OUT / "target")],
                                  capture_output=True, timeout=20)
        (OUT / "transfer.stderr").write_bytes(transfer.stderr)
        if transfer.returncode != 0:
            error = (error or "") + " target evidence transfer failed"
    if baseline is not None:
        try:
            assert jcan_read(["--serial", SERIAL, "config-get"], "jcan_config_post.json") == baseline
        except Exception as exc:
            error = (error or "") + " JCAN postflight failed: " + str(exc)
    (OUT / "coordinator_result.json").write_text(json.dumps({
        "go_sent": go, "executor_started": started, "error": error, "frames": frames,
        "downloads": downloads, "uploads": uploads, "nmt_frames": nmts,
        "nonzero_requests": nonzero, "post_motion_boot_observed": post_motion_boot_at is not None,
        "first_host_after_boot": first_host_after_boot, "raw_can_error": first_error_at is not None,
        "host_requests_after_error": host_after_error, "remote_outcome": remote_outcome,
        "jcan_mode": "normal", "jcan_data_frame_commands": 0}, indent=2) + "\n")
print("COORDINATOR_FINISHED error=" + str(error), flush=True)
sys.exit(1 if error else 0)
