#!/usr/bin/env python3
"""Validate the frozen V2 power-loss evidence after the runner assertion defect."""
import json
from collections import Counter
from pathlib import Path

from trial_checks import parse_capture

BASE = Path(__file__).resolve().parent
target = parse_capture((BASE / "target/rk3588_can.log").read_text())
jcan = []
for line in (BASE / "jcan_session.jsonl").read_text().splitlines():
    event = json.loads(line)
    if event.get("event") == "frame":
        jcan.append((event["can_id"], bytes.fromhex(event.get("data_hex", ""))))
target_frames = [(ident, data) for _, ident, data in target]
assert target_frames == jcan
assert len(target) == 352
assert not any(ident & 0x20000000 for _, ident, _ in target)

nonzero_index, nonzero_at = next((index, timestamp) for index, (timestamp, ident, data) in enumerate(target)
                                  if ident == 0x601 and data.hex() == "23ff600300000500")
assert sum(ident == 0x601 and data.hex() == "23ff600300000500" for _, ident, data in target) == 1
moving = [(index, timestamp, int.from_bytes(data[4:6], "little", signed=True),
           int.from_bytes(data[6:8], "little", signed=True))
          for index, (timestamp, ident, data) in enumerate(target)
          if index > nonzero_index and ident == 0x181 and len(data) == 8 and data[4:] != bytes(4)]
assert moving and all(left == 0 for _, _, left, _ in moving)

boot_index, boot_at = next((index, timestamp) for index, (timestamp, ident, data) in enumerate(target)
                           if index > moving[-1][0] and ident == 0x701 and data == bytes([0]))
first_host_after_boot = next((index, timestamp, ident, data)
                             for index, (timestamp, ident, data) in enumerate(target)
                             if index > boot_index and ident in (0, 0x201, 0x601))
assert first_host_after_boot[2:] == (0x601, bytes.fromhex("23ff600300000000"))
assert not any(ident == 0x601 and data.hex() == "23ff600300000500"
               for _, ident, data in target[boot_index + 1:])
assert not any(ident == 0 and data == bytes([1, 1]) for _, ident, data in target[boot_index + 1:])

assert target[-18][1:] == (0x601, bytes.fromhex("2b40600000000000"))
status_reply = next(data for _, ident, data in target[boot_index + 1:]
                    if ident == 0x581 and data[:4] == bytes.fromhex("43416000"))
assert status_reply[4:] == bytes.fromhex("40144014")
for subindex in (1, 2, 3):
    request = bytes.fromhex(f"406c60{subindex:02x}00000000")
    request_index = next(index for index, (_, ident, data) in enumerate(target[boot_index + 1:], boot_index + 1)
                         if ident == 0x601 and data == request)
    response = target[request_index + 1]
    assert response[1] == 0x581 and response[2][:4] == bytes.fromhex(f"436c60{subindex:02x}")
    assert response[2][4:] == bytes(4)

executor = (BASE / "target/executor.log").read_text()
assert (BASE / "target/executor.rc").read_text().strip() == "0"
assert "interface_inhibitor_released interface=can0 cleanup=verified" in executor
assert "qualification_complete node=1 operation=13" in executor
assert "qualification_expected_external_loss_absent" not in executor
assert json.loads((BASE / "jcan_config_pre.json").read_text()) == json.loads((BASE / "jcan_config_post.json").read_text())
assert [json.loads(line)["op"] for line in (BASE / "jcan_requests.jsonl").read_text().splitlines()] == ["shutdown"]
operator = json.loads((BASE / "operator_observation.json").read_text())
assert operator["right_wheel_normal_stop"] and operator["left_wheel_stationary"]
assert not operator["restart_after_power_restore"] and not operator["abnormal_sound"]
assert operator["final_drive_power_off"] and operator["site_safe"]
assert json.loads((BASE / "final_interface_down_once/result.json").read_text())["pass"] is True

power_cut = json.loads((BASE / "operator_power_cut.json").read_text())["wall_time"]
power_restore = json.loads((BASE / "operator_power_restore.json").read_text())["wall_time"]
no_restart = json.loads((BASE / "operator_no_restart.json").read_text())["wall_time"]
final_off = json.loads((BASE / "operator_final_power_off.json").read_text())["wall_time"]
analysis = {
    "classification": "PASS_WITH_RUNNER_POST_ASSERTION_DEFECT",
    "physical_power_loss_pass": True,
    "runner_wrapper_pass": False,
    "runner_defect": "The wrapper required an internal successful Status context in stdout; successful main prints only inhibitor release and qualification_complete.",
    "target_and_jcan_exact_sequence_match": True,
    "frames_each": len(target),
    "frame_counts": {hex(key): value for key, value in Counter(ident for _, ident, _ in target).items()},
    "jcan_mode": "normal",
    "jcan_link_layer_ack_active": True,
    "jcan_data_frame_commands": 0,
    "can_errors": 0,
    "nonzero_target_count": 1,
    "first_nonzero_target_timestamp": nonzero_at,
    "moving_tpdo_samples": len(moving),
    "first_moving_tpdo_timestamp": moving[0][1],
    "last_moving_tpdo_timestamp": moving[-1][1],
    "left_nonzero_tpdo_samples": 0,
    "post_motion_boot_timestamp": boot_at,
    "boot_to_first_packed_zero_ms": (first_host_after_boot[1] - boot_at) * 1000,
    "first_host_request_after_boot": {"can_id": hex(first_host_after_boot[2]), "data": first_host_after_boot[3].hex()},
    "final_dual_status_raw": "0x14401440",
    "final_speed_views_zero": True,
    "application_exit_code": 0,
    "operator_power_marker_hold_ms": (power_restore - power_cut) * 1000,
    "operator_no_restart_marker_interval_ms": (no_restart - power_restore) * 1000,
    "operator_final_power_off_marker_delay_ms": (final_off - no_restart) * 1000,
    "operator_acceptance": operator,
    "jcan_config_unchanged": True,
    "final_can0_down": True,
}
(BASE / "analysis.json").write_text(json.dumps(analysis, indent=2) + "\n")
print(json.dumps(analysis, indent=2))
