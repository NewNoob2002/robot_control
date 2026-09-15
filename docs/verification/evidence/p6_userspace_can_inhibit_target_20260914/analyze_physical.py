#!/usr/bin/env python3
"""Analyze the consumed physical attempt without opening hardware."""
import json
from pathlib import Path

from trial_checks import parse_capture

OUT = Path(__file__).resolve().parent
TARGET = OUT / "target"
frames = parse_capture((TARGET / "rk3588_can.log").read_text())
normal = [(ident, payload) for _, ident, payload in frames if not ident & 0x20000000]
errors = [(timestamp, ident, payload) for timestamp, ident, payload in frames if ident & 0x20000000]
jcan_events = [json.loads(line) for line in (OUT / "jcan_session.jsonl").read_text().splitlines()]
jcan = [(event["can_id"], bytes.fromhex(event["data_hex"])) for event in jcan_events if event.get("event") == "frame"]
cursor = 0
matched = 0
for frame in normal:
    while cursor < len(jcan) and jcan[cursor] != frame:
        cursor += 1
    if cursor == len(jcan):
        break
    matched += 1
    cursor += 1
tpdo = [payload for _, ident, payload in frames if ident == 0x181 and len(payload) == 8]
left = [int.from_bytes(payload[4:6], "little", signed=True) for payload in tpdo]
right = [int.from_bytes(payload[6:8], "little", signed=True) for payload in tpdo]
executor = (TARGET / "executor.log").read_text()
postflight = json.loads((TARGET / "can_postflight.json").read_text())
result = {
    "status": "INVALID_NO_EXTERNAL_LOSS",
    "physical_retry_authorized": False,
    "target_frames": len(frames),
    "target_error_frames": len(errors),
    "jcan_frames": len(jcan),
    "target_normal_frames": len(normal),
    "target_normal_matched_in_jcan": matched,
    "nonzero_target_requests": sum(ident == 0x601 and payload.hex() == "23ff600300000500" for _, ident, payload in frames),
    "left_nonzero_tpdo_samples": sum(value != 0 for value in left),
    "right_nonzero_tpdo_samples": sum(value != 0 for value in right),
    "right_tpdo_min": min(right, default=0),
    "right_tpdo_max": max(right, default=0),
    "application_reported_external_loss_absent": "qualification_expected_external_loss_absent" in executor,
    "inhibitor_armed": "interface_inhibitor_armed interface=can0" in executor,
    "inhibitor_down": "interface_inhibitor_down interface=can0" in executor,
    "postflight_interface_down": "UP" not in postflight["flags"],
    "capture_exit": (TARGET / "capture.stderr").read_text().strip(),
    "interpretation": "No CAN error frame or communication loss occurred. The application timed out waiting for external loss, then explicitly requested the helper to set can0 DOWN. candump exited because can0 went down and the runner incorrectly treated that expected exit as a harness failure. This attempt does not validate the helper's physical CAN-error path or delayed-TX suppression.",
}
(OUT / "physical_analysis.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
