#!/usr/bin/env python3
"""Derive the final physical result from retained target and JCAN evidence."""
import json
from pathlib import Path

from trial_checks import parse_capture

OUT = Path(__file__).resolve().parent
TARGET = OUT / "target"
frames = parse_capture((TARGET / "rk3588_can.log").read_text())
errors = [(timestamp, ident, payload) for timestamp, ident, payload in frames if ident & 0x20000000]
normal = [(ident, payload) for _, ident, payload in frames if not ident & 0x20000000]
assert len(errors) == 1
jcan_events = [json.loads(line) for line in (OUT / "jcan_session.jsonl").read_text().splitlines()]
jcan = [(event["can_id"], bytes.fromhex(event["data_hex"])) for event in jcan_events if event.get("event") == "frame"]
indices = []
cursor = 0
for frame in normal:
    while cursor < len(jcan) and jcan[cursor] != frame:
        cursor += 1
    assert cursor < len(jcan), "target frame absent from JCAN capture"
    indices.append(cursor)
    cursor += 1
post_error_jcan = jcan[indices[-1] + 1:]
host_ids = {0, 0x201, 0x601}
tpdo = [payload for _, ident, payload in frames if ident == 0x181 and len(payload) == 8]
left = [int.from_bytes(payload[4:6], "little", signed=True) for payload in tpdo]
right = [int.from_bytes(payload[6:8], "little", signed=True) for payload in tpdo]
interface = json.loads((TARGET / "interface_down.json").read_text())
coordinator = json.loads((OUT / "coordinator_result.json").read_text())
clock = json.loads((OUT / "clock_offset.json").read_text())
minimum_offset_s = min(sample["offset_lower_ms"] for sample in clock["samples"]) / 1000
last_jcan_write = (OUT / "jcan_session.jsonl").stat().st_mtime
capture_after_down_estimate_s = last_jcan_write - interface["down_observed_wall_time"]
capture_after_down_conservative_s = capture_after_down_estimate_s + minimum_offset_s - 2.0
error_time = errors[0][0]
down_delay_ms = (interface["down_observed_wall_time"] - error_time) * 1000
executor = (TARGET / "executor.log").read_text()
pre_config = json.loads((OUT / "jcan_config_pre.json").read_text())["data"]
post_config = json.loads((OUT / "jcan_config_post.json").read_text())["data"]
result = {
    "status": "PASS_USERSPACE_CAN_INHIBITOR_PHYSICAL",
    "retry_required": False,
    "target_frames": len(frames),
    "target_error_frames": len(errors),
    "target_normal_frames": len(normal),
    "jcan_frames": len(jcan),
    "target_normal_frames_matched_in_jcan": len(indices),
    "can_error_timestamp": error_time,
    "interface_down_wall_time": interface["down_observed_wall_time"],
    "error_to_interface_down_ms": down_delay_ms,
    "motion_prompt_to_interface_down_ms": (interface["down_observed_wall_time"] - interface["motion_armed_wall_time"]) * 1000,
    "target_requests_after_error": sum(ident in host_ids for timestamp, ident, _ in frames if timestamp > error_time),
    "jcan_host_requests_after_error_anchor": sum(ident in host_ids for ident, _ in post_error_jcan),
    "coordinator_host_requests_after_down_plus_250ms": coordinator["host_requests_after_down_plus_250ms"],
    "post_error_jcan_frames": len(post_error_jcan),
    "post_error_jcan_ids": sorted({ident for ident, _ in post_error_jcan}),
    "estimated_jcan_capture_after_down_s": capture_after_down_estimate_s,
    "conservative_jcan_capture_after_down_s": capture_after_down_conservative_s,
    "historical_delayed_tx_window_s": 4.360145,
    "left_nonzero_tpdo_samples": sum(value != 0 for value in left),
    "right_nonzero_tpdo_samples": sum(value != 0 for value in right),
    "right_tpdo_min": min(right, default=0),
    "right_tpdo_max": max(right, default=0),
    "application_external_bus_error": "qualification_external_bus_error" in executor,
    "inhibitor_armed": "interface_inhibitor_armed interface=can0" in executor,
    "inhibitor_down": "interface_inhibitor_down interface=can0" in executor,
    "jcan_config_unchanged": pre_config == post_config,
    "target_wrapper_pass": json.loads((TARGET / "wrapper_result.json").read_text())["pass"],
    "coordinator_exit_note": "The coordinator stopped at the fixed 100000-frame validation bound after the required observation window; retained frames and live counters satisfy the delayed-TX acceptance condition.",
}
assert result["error_to_interface_down_ms"] <= 1000
assert result["target_requests_after_error"] == 0
assert result["jcan_host_requests_after_error_anchor"] == 0
assert result["coordinator_host_requests_after_down_plus_250ms"] == 0
assert result["conservative_jcan_capture_after_down_s"] > result["historical_delayed_tx_window_s"]
assert result["left_nonzero_tpdo_samples"] == 0 and result["right_nonzero_tpdo_samples"] > 0
assert result["application_external_bus_error"] and result["inhibitor_armed"] and result["inhibitor_down"]
assert result["jcan_config_unchanged"] and result["target_wrapper_pass"]
(OUT / "physical_analysis.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
