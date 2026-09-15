#!/usr/bin/env python3
"""Keep one normal-receive JCAN session alive for the authorized soak."""
import json
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "jcan_ack_keeper_once"
JCAN = Path("/home/gtc/Desktop/workspace/JCAN/target/release/jcan")
SERIAL = "207F346D5650"
STOP_REQUESTED = False


def checked_event(line):
    """Decode one successful JCAN session event."""
    event = json.loads(line)
    assert event.get("ok") is not False and not event.get("warnings") and not event.get("error"), event
    assert event.get("event") in ("session_started", "connected", "frame"), event
    return event


def self_check():
    """Check the small event gate without opening USB."""
    assert checked_event('{"event":"session_started"}')["event"] == "session_started"
    try:
        checked_event('{"ok":false,"error":"x"}')
    except AssertionError:
        print("PASS: JCAN ACK keeper event gate")
        return 0
    raise AssertionError("failed event rejected as success")


def stop_requested(_signum, _frame):
    """Request bounded JCAN shutdown after SIGINT or SIGTERM."""
    global STOP_REQUESTED
    STOP_REQUESTED = True


def jcan_read(arguments, filename):
    """Run and retain one read-only JCAN command."""
    result = subprocess.run([str(JCAN), "--json", *arguments], capture_output=True, timeout=10)
    (OUT / filename).write_bytes(result.stdout)
    (OUT / (filename + ".stderr")).write_bytes(result.stderr)
    assert result.returncode == 0 and not result.stderr, result.stderr.decode(errors="replace")
    value = json.loads(result.stdout)
    assert value.get("ok") is True and not value.get("warnings"), value
    return value["data"]


def main():
    """Start JCAN, retain its capture, and wait for an explicit STOP file."""
    OUT.mkdir(exist_ok=False)
    signal.signal(signal.SIGINT, stop_requested)
    signal.signal(signal.SIGTERM, stop_requested)
    process = None
    baseline = None
    connected = threading.Event()
    reader_error = []
    threads = []
    error = None

    def read_stdout():
        with (OUT / "jcan_session.jsonl").open("wb") as log:
            for line in iter(process.stdout.readline, b""):
                log.write(line)
                log.flush()
                try:
                    event = checked_event(line)
                    if event["event"] == "connected":
                        assert event["serial"] == SERIAL and event["mode"] == "normal"
                        connected.set()
                except BaseException as exc:
                    reader_error.append(str(exc))
                    break

    try:
        assert jcan_read(["self-test"], "self_test.json")["passed"]
        assert any(item["serial"] == SERIAL for item in jcan_read(["scan"], "scan.json"))
        baseline = jcan_read(["--serial", SERIAL, "config-get"], "config_pre.json")
        assert baseline["can_speed"] == "0C" and baseline["standard"] == "00" and baseline["term_res"] == "00"
        process = subprocess.Popen([str(JCAN), "--json", "--serial", SERIAL, "session", "--mode", "normal", "--receive"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        threads = [threading.Thread(target=read_stdout, daemon=True),
                   threading.Thread(target=lambda: (OUT / "jcan.stderr").write_bytes(process.stderr.read()), daemon=True)]
        for thread in threads:
            thread.start()
        assert connected.wait(10) and not reader_error and process.poll() is None, "JCAN normal receive did not become ready"
        (OUT / "READY").write_text(str(process.pid) + "\n")
        print("JCAN_ACK_READY: normal receive connected; ACK active; data-frame commands=0", flush=True)
        deadline = time.monotonic() + 4 * 3600
        while not STOP_REQUESTED and not (OUT / "STOP").exists() and process.poll() is None and not reader_error:
            if time.monotonic() >= deadline:
                raise RuntimeError("four-hour ACK keeper limit")
            time.sleep(0.2)
        if process.poll() is not None and not STOP_REQUESTED and not (OUT / "STOP").exists():
            raise RuntimeError(f"JCAN exited unexpectedly rc={process.returncode}")
        if reader_error:
            raise RuntimeError(reader_error[0])
    except BaseException as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if process is not None and process.poll() is None:
            request = json.dumps({"id": 99, "op": "shutdown"}) + "\n"
            (OUT / "requests.jsonl").write_text(request)
            try:
                process.stdin.write(request.encode())
                process.stdin.flush()
                process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired) as exc:
                process.terminate()
                process.wait(timeout=3)
                error = error or f"JCAN shutdown: {exc}"
        for thread in threads:
            thread.join(timeout=2)
        if (OUT / "jcan.stderr").exists() and (OUT / "jcan.stderr").read_bytes():
            error = error or "JCAN stderr is not empty"
        if baseline is not None:
            try:
                assert jcan_read(["--serial", SERIAL, "config-get"], "config_post.json") == baseline
            except BaseException as exc:
                error = error or f"postflight: {exc}"
    (OUT / "result.json").write_text(json.dumps({"error": error, "jcan_mode": "normal",
        "link_layer_ack": True, "data_frame_commands": 0,
        "process_rc": None if process is None else process.returncode}, indent=2) + "\n")
    print("JCAN_ACK_STOPPED error=" + str(error), flush=True)
    return 1 if error else 0


if __name__ == "__main__":
    sys.exit(self_check() if sys.argv[1:] == ["--self-check"] else main())
