#!/usr/bin/env python3
"""Classify the accepted X1 V2 evidence while retaining the runner assertion defect."""
import json
from pathlib import Path

from trial_checks import parse_capture

BASE = Path(__file__).resolve().parent
TARGET = BASE / "target"
target = parse_capture((TARGET / "rk3588_can.log").read_text())
jcan_events = [json.loads(line) for line in (BASE / "jcan_session.jsonl").read_text().splitlines()]
jcan = [(event["can_id"], bytes.fromhex(event["data_hex"]))
        for event in jcan_events if event.get("event") == "frame"]
target_frames = [(ident, payload) for _, ident, payload in target]
assert target_frames == jcan
assert len(target) == 1989 and not any(ident & 0x20000000 for _, ident, _ in target)

allowed_uploads = {(0x6060, 0), (0x6061, 0), (0x60FF, 1), (0x60FF, 2), (0x603F, 0),
                   (0x6041, 0), (0x1017, 0), (0x1800, 1), (0x1800, 2), (0x1800, 5),
                   (0x1A00, 0), (0x1A00, 1), (0x1A00, 2)}
allowed_uploads |= {(0x606C, part) for part in (1, 2, 3)}
statuses = []
tpdo_timestamps = []
for timestamp, ident, payload in target:
    assert ident in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701), hex(ident)
    if ident == 0:
        assert payload in (bytes([0x80, 1]), bytes([1, 1]))
    elif ident == 0x181:
        assert len(payload) == 8 and payload[4:] == bytes(4)
        statuses.append(int.from_bytes(payload[:4], "little"))
        tpdo_timestamps.append(timestamp)
    elif ident in (0x281, 0x381, 0x481):
        assert len(payload) == 0
    elif ident == 0x581:
        assert len(payload) == 8 and payload[0] != 0x80
    elif ident == 0x601:
        assert len(payload) == 8
        command = payload[0]
        index = int.from_bytes(payload[1:3], "little")
        sub = payload[3]
        if command == 0x40:
            assert (index, sub) in allowed_uploads and payload[4:] == bytes(4)
        else:
            value = int.from_bytes(payload[4:], "little")
            assert (command, index, sub, value) in ((0x2B, 0x1017, 0, 500), (0x2B, 0x1017, 0, 0))

low = [bool(value & 0x8000) for value in statuses]
high = [bool(value & 0x80000000) for value in statuses]
first_active_index = low.index(True)
first_inactive_after = next(index for index in range(first_active_index + 1, len(low)) if not low[index])
assert not any(low[:first_active_index]) and not any(low[first_inactive_after:])
assert not any(high)
assert set(statuses) == {0x14001400, 0x14009400}

executor = (TARGET / "executor.log").read_text()
assert (TARGET / "executor.rc").read_text().strip() == "0"
assert "qualification_complete node=1 operation=14" in executor
assert "qualification_manual_drive_enabled" not in executor
assert not (TARGET / "capture.stderr").read_text().strip()
assert all(int(value) == 0 for line in executor.splitlines() for key in ("tpdo_velocity_raw=", " raw=")
           if key in line for value in [line.rsplit(key, 1)[1].split()[0]])

locked = json.loads((BASE / "operator_estop_locked.json").read_text())
reset = json.loads((BASE / "operator_estop_reset.json").read_text())
no_motion = json.loads((BASE / "operator_no_motion.json").read_text())
operator = json.loads((TARGET / "operator_observation.json").read_text())
assert reset["wall_time"] - locked["wall_time"] >= 3
assert no_motion["wall_time"] - reset["wall_time"] >= 5
assert operator["wheels_stayed_still"] and not operator["abnormal_sound"] and operator["final_drive_power_off"]
final_link = json.loads((BASE / "final_interface_down_once/after.json").read_text())
assert "UP" not in final_link["flags"] and final_link["linkinfo"]["info_data"]["state"] == "STOPPED"
coordinator = json.loads((BASE / "coordinator_result.json").read_text())
wrapper = json.loads((TARGET / "wrapper_result.json").read_text())
assert coordinator["jcan_mode"] == "silent" and coordinator["jcan_data_frame_commands"] == 0
assert wrapper["pass"] is False and wrapper["executor_stopped"] and wrapper["capture_stopped"]
assert json.loads((BASE / "jcan_config_pre.json").read_text()) == json.loads((BASE / "jcan_config_post.json").read_text())

analysis = {
    "classification": "PASS_WITH_RUNNER_STATUS_HALF_EXPECTATION_DEFECT",
    "x1_zero_motion_pretest_pass": True,
    "runner_wrapper_pass": False,
    "runner_defect": "The runner required statusword bit 15 on both packed halves; physical X1 changed only the low half.",
    "target_and_jcan_exact_sequence_match": True,
    "frames_each": len(target),
    "can_errors": 0,
    "rpdo_target_controlword_requests": 0,
    "tpdo1_samples": len(statuses),
    "nonzero_velocity_samples": 0,
    "status_values": ["0x14001400", "0x14009400"],
    "x1_status_half": "low",
    "low_half_bit15_active_samples": sum(low),
    "high_half_bit15_active_samples": sum(high),
    "observed_bit15_active_ms": (tpdo_timestamps[first_inactive_after] - tpdo_timestamps[first_active_index]) * 1000,
    "operator_locked_to_reset_ms": (reset["wall_time"] - locked["wall_time"]) * 1000,
    "operator_reset_to_no_motion_ms": (no_motion["wall_time"] - reset["wall_time"]) * 1000,
    "application_exit_code": 0,
    "operator_acceptance": operator,
    "final_can0_down": True,
    "scope_limit": "Validates X1 electrical/status and reset behavior at zero motion; does not establish moving stop time or distance.",
}
(BASE / "analysis.json").write_text(json.dumps(analysis, indent=2) + "\n")
print(json.dumps(analysis, indent=2))
