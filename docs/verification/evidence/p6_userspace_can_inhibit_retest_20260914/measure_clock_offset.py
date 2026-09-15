#!/usr/bin/env python3
"""Bound host/target wall-clock offset for retained cross-host timestamps."""
import json
import subprocess
import time
from pathlib import Path

OUT = Path(__file__).resolve().parent
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev", "date +%s.%N"]
samples = []
for _ in range(5):
    before = time.time()
    remote = float(subprocess.check_output(SSH, text=True, timeout=10).strip())
    after = time.time()
    samples.append({
        "round_trip_ms": (after - before) * 1000,
        "offset_midpoint_ms": (remote - (before + after) / 2) * 1000,
        "offset_lower_ms": (remote - after) * 1000,
        "offset_upper_ms": (remote - before) * 1000,
    })
result = {"samples": samples, "maximum_round_trip_ms": max(item["round_trip_ms"] for item in samples)}
(OUT / "clock_offset.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
