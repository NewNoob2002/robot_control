#!/usr/bin/env python3
"""Run or analyze the Phase 6 disabled, zero-motion CANopen soak."""
import argparse
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import tarfile
import time
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_ELF = Path("/opt/robot-control/staging/emergency-input-zero-v2-bb4f44251672/robot-control-zlac-qualification")
ELF_SHA256 = "bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7"
MACHINE_ID = "6923ab3301fb4a8d816759b04ec6bf0a"
STOP_REQUESTED = False


def write_json(path, value):
    """Write one stable, newline-terminated JSON document."""
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def snapshot(interface):
    """Read one SocketCAN interface snapshot without transmitting."""
    return json.loads(subprocess.check_output(
        ["ip", "-j", "-details", "-statistics", "link", "show", interface],
        text=True, timeout=5))[0]


def health_counters(link):
    """Extract cumulative CAN error and drop counters used for delta checks."""
    result = {}
    for side in ("rx", "tx"):
        values = link.get("stats64", {}).get(side, {})
        for key in ("errors", "dropped", "over_errors", "missed_errors", "fifo_errors",
                    "aborted_errors", "carrier_errors", "window_errors", "heartbeat_errors"):
            if key in values:
                result[f"{side}.{key}"] = values[key]
    for key, value in link.get("linkinfo", {}).get("info_data", {}).get("berr_counter", {}).items():
        result[f"berr.{key}"] = value
    return result


def health_failures(baseline, current):
    """Return counter names that increased from the retained baseline."""
    before = health_counters(baseline)
    after = health_counters(current)
    return {key: {"before": before.get(key, 0), "after": value}
            for key, value in after.items() if value > before.get(key, 0)}


def validate_cycle_output(text):
    """Validate one complete manual-TPDO cycle log."""
    assert text.count("MANUAL_ROTATION_READY") == 1, "missing or repeated ready marker"
    assert text.count("MANUAL_ROTATION_END") == 1, "missing or repeated end marker"
    samples = [int(value) for value in re.findall(r"tpdo_velocity_raw=(-?\d+)", text)]
    velocities = [int(value) for value in re.findall(r"manual_velocity sub=\d raw=(-?\d+)", text)]
    assert len(samples) >= 50 and len(velocities) >= 150, (len(samples), len(velocities))
    assert not any(samples) and not any(velocities), "nonzero sampled velocity"
    return {"manual_samples": len(samples), "sdo_velocity_samples": len(velocities)}


def parse_capture(text, interface="can0"):
    """Parse complete candump records, including error-frame identifiers."""
    records = []
    previous_error = False
    pattern = re.compile(rf"\s*\(([0-9]+\.[0-9]+)\)\s+{re.escape(interface)}\s+([0-9A-Fa-f]+)\s+\[([0-8])\]\s*(.*?)\s*")
    for line in text.splitlines(keepends=True):
        if not line.endswith("\n"):
            break
        match = pattern.fullmatch(line)
        if match is None:
            assert previous_error and line[:1].isspace() and not line.lstrip().startswith("("), line
            continue
        timestamp, ident, dlc = float(match[1]), int(match[2], 16), int(match[3])
        tokens = match[4].split()
        assert len(tokens) >= dlc and all(re.fullmatch(r"[0-9A-Fa-f]{2}", item) for item in tokens[:dlc]), line
        previous_error = bool(ident & 0x20000000)
        assert tokens[dlc:] == (["ERRORFRAME"] if previous_error else []), line
        records.append((timestamp, ident, bytes.fromhex("".join(tokens[:dlc]))))
    return records


def validate_can_records(records):
    """Validate the exact zero-motion soak traffic and feedback envelope."""
    assert records and not any(ident & 0x20000000 for _, ident, _ in records), "CAN error frame"
    tpdo = 0
    requests = 0
    for _, ident, payload in records:
        assert ident in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701), hex(ident)
        if ident == 0:
            assert payload in (bytes([0x80, 1]), bytes([1, 1])), payload.hex()
        elif ident == 0x181:
            assert len(payload) == 8 and payload[4:] == bytes(4), payload.hex()
            status = int.from_bytes(payload[:4], "little")
            assert status & 0x8000 and not status & 0x80000000, "X1 must set only the observed low-half status bit 15"
            tpdo += 1
        elif ident in (0x281, 0x381, 0x481):
            assert len(payload) == 0, payload.hex()
        elif ident == 0x581:
            assert len(payload) == 8 and payload[0] != 0x80, payload.hex()
        elif ident == 0x601:
            requests += 1
            assert len(payload) == 8
            command = payload[0]
            index = int.from_bytes(payload[1:3], "little")
            sub = payload[3]
            if command == 0x40:
                allowed = {(0x6060, 0), (0x6061, 0), (0x60FF, 1), (0x60FF, 2), (0x603F, 0),
                           (0x6041, 0), (0x1017, 0), (0x1800, 1), (0x1800, 2), (0x1800, 5),
                           (0x1A00, 0), (0x1A00, 1), (0x1A00, 2)}
                allowed |= {(0x606C, part) for part in (1, 2, 3)}
                assert (index, sub) in allowed and payload[4:] == bytes(4), payload.hex()
            else:
                value = int.from_bytes(payload[4:], "little")
                assert (command, index, sub, value) in ((0x2B, 0x1017, 0, 500), (0x2B, 0x1017, 0, 0)), payload.hex()
    assert tpdo > 0 and requests > 0
    return {"frames": len(records), "tpdo1_frames": tpdo, "sdo_requests": requests}


def handle_stop(signum, _frame):
    """Record one operator or service stop request for bounded child cleanup."""
    global STOP_REQUESTED
    STOP_REQUESTED = True
    sys.stderr.write(f"stop requested by signal {signum}\n")
    sys.stderr.flush()


def assert_target_ready(interface, elf):
    """Validate the fixed target, artifact, interface, and exclusive-process preconditions."""
    assert Path("/etc/machine-id").read_text().strip() == MACHINE_ID, "wrong target machine"
    assert hashlib.sha256(elf.read_bytes()).hexdigest() == ELF_SHA256, "qualification ELF mismatch"
    link = snapshot(interface)
    info = link["linkinfo"]["info_data"]
    assert "UP" in link["flags"] and link["mtu"] == 16 and info["state"] == "ERROR-ACTIVE"
    assert info["bittiming"]["bitrate"] == 500000 and all(value == 0 for value in info.get("berr_counter", {}).values())
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        try:
            name = (entry / "exe").resolve(strict=True).name
            assert name not in ("cansend", "cangen", "robot-control-zlac-qualification",
                                "robot-control-canopen-commission"), (entry.name, name)
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass
    return link


def archive_directory(directory):
    """Write checksums and one gzip archive containing the complete soak evidence."""
    files = {}
    for path in sorted(directory.iterdir()):
        if path.is_file() and path.name != "manifest.json":
            files[path.name] = {"bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    write_json(directory / "manifest.json", {"schema_version": 1, "files": files})
    archive = directory.with_suffix(".tar.gz")
    with tarfile.open(archive, "w:gz") as output:
        for path in sorted(directory.iterdir()):
            if path.is_file():
                output.add(path, arcname=f"{directory.name}/{path.name}")
    return archive


def run_soak(args):
    """Run repeated 60-second disabled observation cycles until the requested duration."""
    global STOP_REQUESTED
    assert args.confirm == "X1_LOCKED_WHEELS_RAISED", "missing exact physical precondition confirmation"
    assert 1 <= args.hours <= 12
    pre = assert_target_ready(args.interface, args.elf)
    output = args.output or Path.cwd() / ("phase6-zero-motion-soak-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / "preflight.json", {"time_utc": datetime.now(timezone.utc).isoformat(), "interface": pre,
        "operator_confirmation": args.confirm, "hours": args.hours, "elf": str(args.elf), "elf_sha256": ELF_SHA256})
    signal.signal(signal.SIGINT, handle_stop)
    signal.signal(signal.SIGTERM, handle_stop)
    start = time.monotonic()
    deadline = start + args.hours * 3600
    capture = current = None
    cycles = []
    failure = None
    try:
        with (output / "rk3588_can.log").open("wb") as can_log, (output / "candump.stderr").open("wb") as can_error, \
             (output / "soak.log").open("a+b") as soak_log, (output / "cycle_results.jsonl").open("a") as cycle_log, \
             (output / "can_health.jsonl").open("a") as health_log:
            capture = subprocess.Popen(["stdbuf", "-oL", "candump", "-D", "-ta", "-e",
                                        f"{args.interface},0:0,#FFFFFFFF"], stdout=can_log, stderr=can_error)
            time.sleep(0.25)
            assert capture.poll() is None and not (output / "candump.stderr").read_text()
            cycle = 0
            while time.monotonic() < deadline and not STOP_REQUESTED:
                cycle += 1
                before = snapshot(args.interface)
                health_log.write(json.dumps({"cycle": cycle, "phase": "before", "monotonic": time.monotonic(), "link": before}) + "\n")
                health_log.flush()
                offset = soak_log.tell()
                soak_log.write(f"\n=== cycle {cycle} start_utc={datetime.now(timezone.utc).isoformat()} ===\n".encode())
                soak_log.flush()
                current = subprocess.Popen([str(args.elf), "--interface", args.interface, "--manual-tpdo"],
                                           stdout=soak_log, stderr=subprocess.STDOUT)
                terminate_sent = False
                while current.poll() is None:
                    if STOP_REQUESTED and not terminate_sent:
                        current.send_signal(signal.SIGTERM)
                        terminate_sent = True
                    time.sleep(0.1)
                soak_log.flush()
                end = soak_log.tell()
                soak_log.seek(offset)
                cycle_text = soak_log.read(end - offset).decode(errors="replace")
                soak_log.seek(end)
                after = snapshot(args.interface)
                health_log.write(json.dumps({"cycle": cycle, "phase": "after", "monotonic": time.monotonic(), "link": after}) + "\n")
                health_log.flush()
                record = {"cycle": cycle, "returncode": current.returncode, "started_offset": offset,
                          "ended_offset": end, "elapsed_s": time.monotonic() - start}
                if not STOP_REQUESTED:
                    record |= validate_cycle_output(cycle_text)
                    assert current.returncode == 0, current.returncode
                    assert "UP" in after["flags"] and after["linkinfo"]["info_data"]["state"] == "ERROR-ACTIVE"
                    increased = health_failures(pre, after)
                    assert not increased, increased
                    record["pass"] = True
                else:
                    record["pass"] = False
                    record["interrupted"] = True
                cycles.append(record)
                cycle_log.write(json.dumps(record) + "\n")
                cycle_log.flush()
                assert capture.poll() is None, "candump exited"
    except BaseException as exc:
        failure = f"{type(exc).__name__}: {exc}"
    finally:
        if current is not None and current.poll() is None:
            current.send_signal(signal.SIGTERM)
            try:
                current.wait(timeout=7)
            except subprocess.TimeoutExpired:
                current.kill()
                current.wait(timeout=2)
        if capture is not None and capture.poll() is None:
            capture.send_signal(signal.SIGINT)
            try:
                capture.wait(timeout=3)
            except subprocess.TimeoutExpired:
                capture.kill()
                capture.wait(timeout=2)
        try:
            post = snapshot(args.interface)
            write_json(output / "postflight.json", post)
        except BaseException as exc:
            post = None
            failure = failure or f"postflight: {exc}"
        complete = failure is None and not STOP_REQUESTED and time.monotonic() >= deadline and bool(cycles)
        result = {"status": "PASS" if complete else "INTERRUPTED" if STOP_REQUESTED and failure is None else "FAIL",
                  "requested_hours": args.hours, "elapsed_s": time.monotonic() - start, "cycles": len(cycles),
                  "failure": failure, "capture_stopped": capture is None or capture.poll() is not None,
                  "executor_stopped": current is None or current.poll() is not None,
                  "x1_must_remain_locked_until_drive_power_off": True, "drive_power_off_confirmed": False}
        write_json(output / "result.json", result)
        archive = archive_directory(output)
    print(json.dumps(result, indent=2))
    print(f"ARCHIVE={archive}")
    return 0 if result["status"] == "PASS" else 1


def load_evidence(path):
    """Load evidence files from a run directory or its generated tar.gz archive."""
    if path.is_dir():
        return {item.name: item.read_bytes() for item in path.iterdir() if item.is_file()}
    with tarfile.open(path, "r:gz") as archive:
        result = {}
        for member in archive.getmembers():
            assert member.isfile() and ".." not in Path(member.name).parts
            name = Path(member.name).name
            assert name not in result, name
            result[name] = archive.extractfile(member).read()
        return result


def analyze_soak(args):
    """Analyze one completed soak directory or archive without hardware access."""
    files = load_evidence(args.evidence)
    required = {"manifest.json", "result.json", "preflight.json", "postflight.json", "soak.log",
                "rk3588_can.log", "can_health.jsonl", "cycle_results.jsonl", "candump.stderr"}
    assert required <= files.keys(), sorted(required - files.keys())
    manifest = json.loads(files["manifest.json"])
    for name, expected in manifest["files"].items():
        assert name in files and len(files[name]) == expected["bytes"]
        assert hashlib.sha256(files[name]).hexdigest() == expected["sha256"], name
    result = json.loads(files["result.json"])
    assert result["status"] == "PASS" and result["drive_power_off_confirmed"] is False
    assert result["capture_stopped"] and result["executor_stopped"]
    cycles = [json.loads(line) for line in files["cycle_results.jsonl"].decode().splitlines()]
    assert len(cycles) == result["cycles"] and cycles and all(item["pass"] and item["returncode"] == 0 for item in cycles)
    soak_log = files["soak.log"]
    for item in cycles:
        checks = validate_cycle_output(soak_log[item["started_offset"]:item["ended_offset"]].decode(errors="replace"))
        assert checks["manual_samples"] == item["manual_samples"]
        assert checks["sdo_velocity_samples"] == item["sdo_velocity_samples"]
    assert not files["candump.stderr"].strip()
    can = validate_can_records(parse_capture(files["rk3588_can.log"].decode()))
    pre = json.loads(files["preflight.json"])["interface"]
    post = json.loads(files["postflight.json"])
    health = [json.loads(line) for line in files["can_health.jsonl"].decode().splitlines()]
    assert len(health) == 2 * len(cycles)
    for item in health:
        link = item["link"]
        assert not health_failures(pre, link)
        assert "UP" in link["flags"] and link["linkinfo"]["info_data"]["state"] == "ERROR-ACTIVE"
    increased = health_failures(pre, post)
    assert not increased and "UP" in post["flags"] and post["linkinfo"]["info_data"]["state"] == "ERROR-ACTIVE"
    analysis = {"status": "PASS_PHASE6_ZERO_MOTION_SOAK", "cycles": len(cycles),
                "elapsed_s": result["elapsed_s"], "can": can, "counter_increases": increased,
                "scope": "Repeated 60-second disabled/NMT/heartbeat/TPDO lifecycle soak; no motion or load claim.",
                "operator_next_step": "Keep X1 locked, power the drive OFF, then restore X1 only after both wheels are stopped."}
    destination = args.output or (args.evidence / "analysis.json" if args.evidence.is_dir() else Path(str(args.evidence) + ".analysis.json"))
    write_json(destination, analysis)
    print(json.dumps(analysis, indent=2))
    return 0


def parse_args(argv):
    """Parse the bounded run and offline-analysis command lines."""
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="run the target soak")
    run.add_argument("--hours", type=int, default=10, choices=range(1, 13))
    run.add_argument("--interface", default="can0")
    run.add_argument("--elf", type=Path, default=DEFAULT_ELF)
    run.add_argument("--output", type=Path)
    run.add_argument("--confirm", required=True, help="must be X1_LOCKED_WHEELS_RAISED")
    analyze = sub.add_parser("analyze", help="analyze a directory or tar.gz")
    analyze.add_argument("evidence", type=Path)
    analyze.add_argument("--output", type=Path)
    return parser.parse_args(argv)


def main(argv=None):
    """Dispatch the requested target run or offline analysis."""
    args = parse_args(argv)
    return run_soak(args) if args.command == "run" else analyze_soak(args)


if __name__ == "__main__":
    sys.exit(main())
