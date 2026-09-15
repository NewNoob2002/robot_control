#!/usr/bin/env python3
"""Classify the consumed moving X1 trial without opening hardware."""
import json
from pathlib import Path

from trial_checks import _moving_stop, parse_capture

BASE = Path(__file__).resolve().parent
TARGET = BASE / "target"
target = parse_capture((TARGET / "rk3588_can.log").read_text())
jcan_events = [json.loads(line) for line in (BASE / "jcan_session.jsonl").read_text().splitlines()]
jcan = [(event["can_id"], bytes.fromhex(event["data_hex"]))
        for event in jcan_events if event.get("event") == "frame"]
target_frames = [(ident, payload) for _, ident, payload in target]
assert target_frames == jcan

samples, requests, target_on, target_off, first_motion, active, stable_zero = _moving_stop(target)
active_samples = [sample for sample in samples if sample[1] & 0x8000]
assert active_samples and all(not status & 0x80000000 for _, status, _, _ in samples)
assert all(left == 0 for _, _, left, _ in samples)
assert not any(right for stamp, _, _, right in samples if stamp >= stable_zero)

executor = (TARGET / "motion_executor.log").read_text()
assert "qualification_complete node=1 operation=4" in executor
assert not (TARGET / "capture.stderr").read_text().strip()
wrapper = json.loads((TARGET / "wrapper_result.json").read_text())
assert wrapper["pass"] is False and wrapper["motion_executor_stopped"] and wrapper["capture_stopped"]
remote_error = (BASE / "remote_err.log").read_text()
assert "motion ended before X1 was locked" in remote_error
assert not (TARGET / "manual_started.json").exists()

coordinator = json.loads((BASE / "coordinator_result.json").read_text())
assert coordinator["jcan_mode"] == "silent" and coordinator["jcan_data_frame_commands"] == 0
assert coordinator["target_and_jcan_exact_sequence_match"]
assert json.loads((BASE / "jcan_config_pre.json").read_text()) == json.loads((BASE / "jcan_config_post.json").read_text())

operator = json.loads((BASE / "operator_observation.json").read_text())
assert operator["right_wheel_stopped_normally"] and operator["left_wheel_stayed_still"]
assert not operator["restart_observed"] and not operator["abnormal_sound"]
assert operator["final_drive_power_off"] and operator["x1_reset_after_drive_power_off"]
assert operator["both_wheels_stopped_and_site_safe"]
assert json.loads((BASE / "operator_failure_power_off.json").read_text())["final_drive_power_off"]

final_link = json.loads((BASE / "final_interface_down_once/after.json").read_text())
assert "UP" not in final_link["flags"] and final_link["linkinfo"]["info_data"]["state"] == "STOPPED"
zero_v2 = json.loads((BASE.parent / "p6_emergency_input_v2_20260915/analysis.json").read_text())
assert zero_v2["x1_zero_motion_pretest_pass"] and zero_v2["operator_acceptance"]["final_drive_power_off"]

result = {
    "classification": "PASS_MOVING_X1_STOP_WITH_RUNNER_OPERATOR_MARKER_TIMEOUT",
    "moving_x1_stop_pass": True,
    "runner_wrapper_pass": False,
    "runner_defect": "The runner waited for typed ESTOP_LOCKED before accepting the already captured X1 transition; the application completed its 3000 ms interval first.",
    "physical_retry_required": False,
    "frames_each": len(target),
    "target_and_jcan_exact_sequence_match": True,
    "jcan_mode": "silent",
    "jcan_data_frame_commands": 0,
    "can_errors": 0,
    "sdo_requests": len(requests),
    "left_nonzero_tpdo_samples": 0,
    "right_nonzero_tpdo_samples": sum(right != 0 for _, _, _, right in samples),
    "right_tpdo_min": min(right for _, _, _, right in samples),
    "right_tpdo_max": max(right for _, _, _, right in samples),
    "x1_low_half_active_samples": len(active_samples),
    "x1_high_half_active_samples": 0,
    "target_to_first_motion_ms": (first_motion - target_on) * 1000,
    "first_motion_to_x1_ms": (active - first_motion) * 1000,
    "x1_to_stable_zero_ms": (stable_zero - active) * 1000,
    "stable_zero_before_scheduled_target_zero_ms": (target_off - stable_zero) * 1000,
    "no_restart_through_capture_end": True,
    "motion_executor_completion_logged": True,
    "same_trial_powered_reset_observed": False,
    "powered_zero_target_reset_no_restart_reference": "p6_emergency_input_v2_20260915",
    "phase6_x1_behavior_composite_pass": True,
    "operator_acceptance": operator,
    "final_can0_down": True,
    "scope_limit": "Raised-wheel right +5 rpm X1 stopping plus the separate powered zero-target reset/no-restart V2 result; no loaded stopping distance or certified safety claim.",
}
(BASE / "analysis.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
