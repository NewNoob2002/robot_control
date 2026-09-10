#!/usr/bin/env python3
"""Reconcile the aborted zero-target setup against both independent raw captures."""
from collections import Counter
from decimal import Decimal
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parent
frames = []
for line in (root / 'target/rk3588_can.log').read_text().splitlines():
    match = re.fullmatch(r'\s*\((\d+\.\d+)\)\s+can0\s+([0-9A-Fa-f]+)\s+\[(\d+)\]\s*(.*)', line)
    assert match, line
    timestamp, identifier, length, payload = match.groups()
    data = bytes.fromhex(payload)
    assert len(data) == int(length)
    frames.append((Decimal(timestamp), int(identifier, 16), data))
events = [json.loads(line) for line in (root / 'jcan_session.jsonl').read_text().splitlines()]
assert all(event.get('ok') is not False and not event.get('warnings') and not event.get('error') for event in events)
jcan = [event for event in events if event.get('event') == 'frame']
assert all(not any(f.get(key, False) for key in ('extended', 'fd', 'brs', 'remote')) for f in jcan)
assert [(i, d) for _, i, d in frames] == [(f['can_id'], bytes.fromhex(f['data_hex'])) for f in jcan]
requests = [(t, d) for t, i, d in frames if i == 0x601]
downloads = [(t, d) for t, d in requests if d[0] in (0x2B, 0x2F, 0x23)]
nonzero = [d for _, d in downloads if d[1:3] == bytes.fromhex('FF60') and any(d[4:])]
watchdog_writes = [d for _, d in downloads if d[1:4] == bytes.fromhex('002000')]
assert not nonzero and not watchdog_writes
assert all(i in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701) for _, i, _ in frames)
readbacks = {}
for _, identifier, data in frames:
    if identifier == 0x581 and data[0] in (0x43, 0x4B, 0x4F):
        readbacks[f'{int.from_bytes(data[1:3], "little"):04X}:{data[3]:02X}'] = int.from_bytes(data[4:], 'little')
assert all(readbacks[key] == 0 for key in ('2000:00', '1017:00', '60FF:01', '60FF:02', '606C:01', '606C:02', '606C:03'))
assert readbacks['200F:00'] == 1
tpdos = [(t, d) for t, i, d in frames if i == 0x181]
assert all(d == bytes.fromhex('0714071400000000') for _, d in tpdos)
shutdowns = [(t, d) for t, d in downloads if d == bytes.fromhex('2B40600006000000')]
assert len(shutdowns) == 2
ack_ms = []
for time, _ in shutdowns:
    ack = next(t for t, i, d in frames if t > time and i == 0x581 and d == bytes.fromhex('6040600000000000'))
    ack_ms.append(float((ack - time) * 1000))
pre = json.loads((root / 'target/can_preflight.json').read_text())[0]
post = json.loads((root / 'target/can_postflight.json').read_text())[0]
delta = {side: post['stats64'][side]['packets'] - pre['stats64'][side]['packets'] for side in ('rx', 'tx')}
assert delta['tx'] == sum(i in (0, 0x601) for _, i, _ in frames)
assert delta['rx'] == len(frames) - delta['tx']
assert all(post['stats64'][side][key] == 0 for side in ('rx', 'tx') for key in ('errors', 'dropped'))
assert post['linkinfo']['info_data']['state'] == 'ERROR-ACTIVE'
assert all(value == 0 for value in post['linkinfo']['info_xstats'].values())
assert json.loads((root / 'jcan_config_pre.json').read_text())['data'] == json.loads((root / 'jcan_config_post.json').read_text())['data']
wrapper = json.loads((root / 'target/wrapper_result.json').read_text())
assert wrapper['capture_stopped'] and wrapper['executor_stopped'] and wrapper['wrapper_exit'] == 1
assert (root / 'target/capture.rc').read_text() == '0'
assert (root / 'target/capture.stderr').stat().st_size == 0
result = {
    'status': 'ABORTED_BEFORE_WATCHDOG_AND_MOTION', 'capture_agreement': True, 'matched_frames': len(frames),
    'frame_ids': {f'{key:03X}': value for key, value in Counter(i for _, i, _ in frames).items()},
    'target_packet_deltas': delta, 'downloads': len(downloads), 'uploads': len(requests) - len(downloads),
    'nonzero_target_requests': 0, 'watchdog_downloads': 0, 'watchdog_baseline': 0,
    'shutdown_ack_ms': ack_ms, 'dual_status_raw': '0x1407/0x1407', 'tpdo_packed_velocity_always_zero': True,
    'independent_velocity_zero': 'preflight only; post-cleanup SDO verification not reached',
    'final_target_readbacks': [readbacks['60FF:01'], readbacks['60FF:02']],
    'restored_application': readbacks['200F:00'], 'restored_heartbeat': readbacks['1017:00'],
    'shutdown_state_verified': False, 'preoperational_heartbeat_verified': False,
    'can_errors_or_drops': 0, 'process_cleanup': 'pass', 'adapter_configuration_unchanged': True,
    'operator_observation': json.loads((root / 'operator_observation.json').read_text()), 'hardware_retry': False,
    'watchdog_requirement': 'OPEN; stimulus not reached', 'heartbeat_and_tpdo_trials': 'not run'
}
(root / 'analysis.json').write_text(json.dumps(result, indent=2) + chr(10))
print(json.dumps(result, indent=2))
