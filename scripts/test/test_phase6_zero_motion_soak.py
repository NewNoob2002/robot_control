#!/usr/bin/env python3
"""Test the Phase 6 soak parsers without CAN hardware."""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "scripts/hil/phase6_zero_motion_soak.py"
spec = importlib.util.spec_from_file_location("phase6_zero_motion_soak", MODULE)
soak = importlib.util.module_from_spec(spec)
spec.loader.exec_module(soak)


def expect_failure(call):
    """Require one validation callable to raise AssertionError."""
    try:
        call()
    except AssertionError:
        return
    raise AssertionError("validation unexpectedly passed")


def main():
    """Exercise zero/nonzero logs, exact traffic, X1 state, and counter deltas."""
    cycle = "MANUAL_ROTATION_READY\n" + "".join(
        f"manual_sample elapsed_ms={item * 1000} phase=stationary tpdo_status_raw=2493548128 tpdo_velocity_raw=0\n"
        + "manual_velocity sub=1 raw=0\nmanual_velocity sub=2 raw=0\nmanual_velocity sub=3 raw=0\n"
        for item in range(60)) + "MANUAL_ROTATION_END\n"
    assert soak.validate_cycle_output(cycle) == {"manual_samples": 60, "sdo_velocity_samples": 180}
    expect_failure(lambda: soak.validate_cycle_output(cycle.replace("tpdo_velocity_raw=0", "tpdo_velocity_raw=1", 1)))
    expect_failure(lambda: soak.validate_cycle_output(cycle.replace("manual_velocity sub=2 raw=0", "manual_velocity sub=2 raw=-1", 1)))
    status = (0x9460 | (0x1460 << 16)).to_bytes(4, "little")
    frames = [(1.0, 0x181, status + bytes(4)),
              (1.1, 0x601, bytes.fromhex("4017100000000000")),
              (1.2, 0x601, bytes.fromhex("2b171000f4010000")),
              (1.3, 0, bytes([1, 1])),
              (1.4, 0x581, bytes.fromhex("60171000f4010000"))]
    assert soak.validate_can_records(frames)["tpdo1_frames"] == 1
    expect_failure(lambda: soak.validate_can_records([(1.0, 0x181, bytes(8)), *frames[1:]]))
    both_active = (0x9460 | (0x9460 << 16)).to_bytes(4, "little")
    expect_failure(lambda: soak.validate_can_records([(1.0, 0x181, both_active + bytes(4)), *frames[1:]]))
    expect_failure(lambda: soak.validate_can_records([*frames, (2.0, 0x601, bytes.fromhex("2340600000000000"))]))
    baseline = {"stats64": {"rx": {"errors": 2, "dropped": 3}, "tx": {"errors": 4, "dropped": 5}},
                "linkinfo": {"info_data": {"berr_counter": {"rx": 0, "tx": 0}}}}
    assert not soak.health_failures(baseline, baseline)
    changed = {**baseline, "stats64": {**baseline["stats64"], "rx": {"errors": 3, "dropped": 3}}}
    assert soak.health_failures(baseline, changed) == {"rx.errors": {"before": 2, "after": 3}}
    # A graceful candump exit is still lost evidence, even with return code zero.
    for code in (0, -15):
        expect_failure(lambda: soak.assert_capture_running(SimpleNamespace(poll=lambda: code)))
    soak.assert_capture_running(SimpleNamespace(poll=lambda: None))
    with tempfile.TemporaryDirectory() as temporary:
        fake = Path(temporary) / "candump"
        fake.write_text("#!/usr/bin/env python3\nimport signal,time\nsignal.signal(signal.SIGHUP, lambda *_: exit(0))\nprint('READY', flush=True)\ntime.sleep(10)\n")
        fake.chmod(0o755)
        # Only the new test session receives HUP; never signal the test runner.
        probe = '''import os,runpy,signal,subprocess
signal.signal(signal.SIGHUP, signal.SIG_IGN)
soak=runpy.run_path(os.environ["SOAK_MODULE"])
capture=soak["start_capture"]("unused", subprocess.PIPE, subprocess.PIPE)
try:
    assert capture.stdout.readline().strip() == b"READY"
    os.killpg(os.getpgrp(), signal.SIGHUP)
    assert os.getsid(capture.pid) == capture.pid
    soak["assert_capture_running"](capture)
finally:
    if capture.poll() is None:
        capture.terminate()
    capture.communicate(timeout=2)
'''
        subprocess.run([sys.executable, "-c", probe], check=True, timeout=5, start_new_session=True,
                       env={**os.environ, "PATH": temporary + os.pathsep + os.environ["PATH"],
                            "SOAK_MODULE": str(MODULE)})
    link = {"flags": ["UP"], "linkinfo": {"info_data": {"state": "ERROR-ACTIVE",
            "berr_counter": {"rx": 0, "tx": 0}}}, "stats64": {"rx": {"errors": 0, "dropped": 0},
            "tx": {"errors": 0, "dropped": 0}}}
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        # Real child processes, but no SocketCAN or qualification binary.
        for name, seconds in (("candump", 0.5), ("fake-qualification", 20)):
            child = root / name
            child.write_text(f"#!/usr/bin/env python3\nimport time\ntime.sleep({seconds})\n")
            child.chmod(0o755)
        with patch.dict(os.environ, {"PATH": temporary + os.pathsep + os.environ["PATH"]}), \
             patch.object(soak, "assert_target_ready", return_value=link), \
             patch.object(soak, "snapshot", return_value=link):
            assert soak.run_soak(SimpleNamespace(confirm="X1_LOCKED_WHEELS_RAISED", hours=1,
                interface="unused", elf=root / "fake-qualification", output=root / "interrupted")) == 1
        result = json.loads((root / "interrupted/result.json").read_text())
        assert result["failure"] == "AssertionError: candump exited rc=0"
        assert result["executor_stopped"] and result["capture_returncode"] == 0
        assert result["elapsed_s"] < 3 and result["cycles"] == 0
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary) / "soak"
        directory.mkdir()
        cycle_bytes = cycle.encode()
        soak.write_json(directory / "preflight.json", {"interface": link})
        soak.write_json(directory / "postflight.json", link)
        soak.write_json(directory / "result.json", {"status": "PASS", "drive_power_off_confirmed": False,
            "capture_stopped": True, "executor_stopped": True, "cycles": 1, "elapsed_s": 3600})
        (directory / "soak.log").write_bytes(cycle_bytes)
        (directory / "cycle_results.jsonl").write_text(json.dumps({"cycle": 1, "pass": True, "returncode": 0,
            "started_offset": 0, "ended_offset": len(cycle_bytes), "manual_samples": 60,
            "sdo_velocity_samples": 180}) + "\n")
        (directory / "can_health.jsonl").write_text(
            json.dumps({"cycle": 1, "phase": "before", "link": link}) + "\n"
            + json.dumps({"cycle": 1, "phase": "after", "link": link}) + "\n")
        (directory / "rk3588_can.log").write_text(
            "(1.000000) can0 181 [8] 60 94 60 14 00 00 00 00\n"
            "(1.100000) can0 601 [8] 40 17 10 00 00 00 00 00\n"
            "(1.200000) can0 601 [8] 2B 17 10 00 F4 01 00 00\n"
            "(1.300000) can0 000 [2] 01 01\n"
            "(1.400000) can0 581 [8] 60 17 10 00 F4 01 00 00\n")
        (directory / "candump.stderr").write_bytes(b"")
        archive = soak.archive_directory(directory)
        analysis = Path(temporary) / "analysis.json"
        assert soak.analyze_soak(SimpleNamespace(evidence=archive, output=analysis)) == 0
        assert json.loads(analysis.read_text())["status"] == "PASS_PHASE6_ZERO_MOTION_SOAK"
    print("PASS: Phase 6 zero-motion soak parsers and fail-closed checks")


if __name__ == "__main__":
    main()
