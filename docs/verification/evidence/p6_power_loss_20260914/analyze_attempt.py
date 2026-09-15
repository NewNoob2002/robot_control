#!/usr/bin/env python3
"""Analyze the consumed drive-power attempt without touching hardware."""
import json
from collections import Counter
from pathlib import Path

from trial_checks import parse_capture

BASE = Path(__file__).resolve().parent
records = parse_capture((BASE / "target/rk3588_can.log").read_text())
power_cut_armed = json.loads((BASE / "target/power_cut_armed.json").read_text())["wall_time"]
interface = json.loads((BASE / "target/interface_down.json").read_text())
operator_cut = json.loads((BASE / "operator_power_cut.json").read_text())["wall_time"]
operator_restore = json.loads((BASE / "operator_power_restore.json").read_text())["wall_time"]
operator_no_restart = json.loads((BASE / "operator_no_restart.json").read_text())["wall_time"]
operator_final_off = json.loads((BASE / "operator_final_power_off.json").read_text())["wall_time"]
nonzero = [(timestamp, data.hex()) for timestamp, ident, data in records
           if ident == 0x601 and data.hex() == "23ff600300000500"]
zero = [(timestamp, data.hex()) for timestamp, ident, data in records
        if ident == 0x601 and data.hex() == "23ff600300000000" and nonzero and timestamp > nonzero[0][0]]
errors = [(timestamp, ident, data.hex()) for timestamp, ident, data in records if ident & 0x20000000]
post_error_requests = [(timestamp, ident, data.hex()) for timestamp, ident, data in records
                       if errors and timestamp > errors[0][0] and ident in (0, 0x201, 0x601)]

jcan_frames = []
for line in (BASE / "jcan_session.jsonl").read_text().splitlines():
    event = json.loads(line)
    if event.get("event") == "frame":
        jcan_frames.append((event["can_id"], bytes.fromhex(event.get("data_hex", ""))))
nonzero_index = next(index for index, frame in enumerate(jcan_frames)
                     if frame == (0x601, bytes.fromhex("23ff600300000500")))
boot_indices = [index for index, frame in enumerate(jcan_frames[nonzero_index + 1:], nonzero_index + 1)
                if frame == (0x701, bytes([0]))]
first_host_after_boot = None
if boot_indices:
    first_host_after_boot = next(((ident, data.hex()) for ident, data in jcan_frames[boot_indices[0] + 1:]
                                  if ident in (0, 0x201, 0x601)), None)

analysis = {
    "classification": "INVALID_STIMULUS_TIMING_AND_CAPTURE_BOUND",
    "physical_power_loss_pass": False,
    "safe_final_state": True,
    "reason": "The application zeroed and inhibited can0 before the operator-confirmed drive power cut, so power loss did not trigger the observed stop. The passive JCAN capture also exceeded its 100000-frame validator bound.",
    "target_frames": len(records),
    "target_frame_counts": {hex(key): value for key, value in Counter(ident for _, ident, _ in records).items()},
    "nonzero_target_count": len(nonzero),
    "first_nonzero_target_timestamp": nonzero[0][0],
    "first_post_motion_zero_timestamp": zero[0][0],
    "target_to_zero_ms": (zero[0][0] - nonzero[0][0]) * 1000,
    "power_cut_arm_to_zero_ms": (zero[0][0] - power_cut_armed) * 1000,
    "first_can_error_timestamp": errors[0][0],
    "error_to_interface_down_ms": (interface["down_observed_wall_time"] - errors[0][0]) * 1000,
    "interface_down_to_operator_power_cut_ms": (operator_cut - interface["down_observed_wall_time"]) * 1000,
    "zero_to_operator_power_cut_ms": (operator_cut - zero[0][0]) * 1000,
    "operator_power_off_hold_ms": (operator_restore - operator_cut) * 1000,
    "operator_no_restart_observation_ms": (operator_no_restart - operator_restore) * 1000,
    "operator_final_power_off_recorded": True,
    "post_error_rk3588_requests": len(post_error_requests),
    "jcan_frames_retained": len(jcan_frames),
    "jcan_frame_counts": {hex(key): value for key, value in Counter(ident for ident, _ in jcan_frames).items()},
    "post_motion_drive_boot_observed": bool(boot_indices),
    "first_host_request_after_drive_boot": first_host_after_boot,
    "jcan_config_unchanged": json.loads((BASE / "jcan_config_pre.json").read_text())
        == json.loads((BASE / "jcan_config_post.json").read_text()),
    "runner_wrapper_final_power_off_false_is_race":
        json.loads((BASE / "target/wrapper_result.json").read_text())["final_drive_power_off"] is False
        and operator_final_off > json.loads((BASE / "target/wrapper_result.json").read_text()).get("written_at", 0),
}
(BASE / "analysis.json").write_text(json.dumps(analysis, indent=2) + "\n")
print(json.dumps(analysis, indent=2))
