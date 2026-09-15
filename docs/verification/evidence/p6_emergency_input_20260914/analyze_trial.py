#!/usr/bin/env python3
"""Derive the X1 zero-motion result from target and JCAN evidence."""
import json
from pathlib import Path

from trial_checks import parse_capture, validate_records

OUT = Path(__file__).resolve().parent
target = validate_records(parse_capture((OUT / "target/rk3588_can.log").read_text()))
jcan_events = [json.loads(line) for line in (OUT / "jcan_session.jsonl").read_text().splitlines()]
jcan_records = [(float(index), event["can_id"], bytes.fromhex(event["data_hex"]))
                for index, event in enumerate(jcan_events) if event.get("event") == "frame"]
jcan = validate_records(jcan_records)
wrapper = json.loads((OUT / "target/wrapper_result.json").read_text())
operator = json.loads((OUT / "target/operator_observation.json").read_text())
coordinator = json.loads((OUT / "coordinator_result.json").read_text())
result = {"status": "PASS_X1_ZERO_MOTION_ELECTRICAL_STATE", "target": target, "jcan": jcan,
          "operator": operator, "jcan_silent": coordinator["jcan_mode"] == "silent",
          "jcan_data_frame_commands": coordinator["jcan_data_frame_commands"],
          "wrapper_pass": wrapper["pass"],
          "scope_limit": "Does not establish moving stop time or distance."}
assert coordinator["error"] is None and result["jcan_silent"] and result["jcan_data_frame_commands"] == 0
assert result["wrapper_pass"] and operator["wheels_stayed_still"] and not operator["abnormal_sound"]
(OUT / "analysis.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
