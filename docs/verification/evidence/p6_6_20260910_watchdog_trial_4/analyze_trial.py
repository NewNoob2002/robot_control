#!/usr/bin/env python3
"""Reconcile the watchdog trial and preserve readback rebound without inferring physical timing."""
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
assert all(i in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701) for _, i, _ in frames)


def matching(identifier, payload):
    """Return timestamps for one exact frame payload."""
    return [t for t, i, d in frames if i == identifier and d == bytes.fromhex(payload)]


def elapsed(now, then):
    """Return milliseconds without losing submillisecond precision to epoch floats."""
    return float((now - then) * 1000)


target = matching(0x601, '23FF600205000000')
assert len(target) == 1
target = target[0]
downloads = [(t, d) for t, i, d in frames if i == 0x601 and d[0] in (0x23, 0x2B, 0x2F)]
assert [(d[3], int.from_bytes(d[4:], 'little', signed=True)) for _, d in downloads
        if d[1:3] == bytes.fromhex('FF60') and any(d[4:])] == [(2, 5)]
assert len(matching(0x601, '2B002000E8030000')) == 1
assert len(matching(0x601, '2B00200000000000')) == 1
last_target_read = next(t for t in matching(0x601, '40FF600200000000') if t > target)
initial_probe = next(t for t in matching(0x601, '406C600100000000') if t > target)
initial_right = next(t for t in matching(0x601, '406C600200000000') if t > initial_probe)
probe = next(t for t in matching(0x601, '406C600100000000') if t > initial_right)
initial_velocities = [(t, d[3], int.from_bytes(d[4:], 'little', signed=True)) for t, i, d in frames
                      if initial_probe < t < probe and i == 0x581 and d[:3] == bytes.fromhex('436C60')]
assert len(initial_velocities) == 2
assert initial_velocities[0][1:] == (1, 0)
assert initial_velocities[1][1] == 2 and initial_velocities[1][2] != 0
assert elapsed(initial_velocities[-1][0], initial_probe) <= 100
last_target_read = max(t for t, i, _ in frames if target < t < probe and i in (0, 0x601))
assert last_target_read == initial_right
assert not [(t, i) for t, i, _ in frames if last_target_read < t < probe and i in (0, 0x601)]
assert elapsed(probe, last_target_read) >= 1500
first_zero = next(t for t in matching(0x601, '23FF600100000000') if t > target)
precleanup_velocities = [(d[3], int.from_bytes(d[4:], 'little', signed=True)) for t, i, d in frames
                        if probe < t < first_zero and i == 0x581 and d[:3] == bytes.fromhex('436C60')]
assert precleanup_velocities == [(1, 0), (2, 0), (3, 0)]
shutdown = next(t for t in matching(0x601, '2B40600006000000') if t > target)
assert not [(t, d) for t, d in downloads if t > target and d[1:3] == bytes.fromhex('4060') and d[4] != 6]
velocity_after_probe = [(t, d[3], int.from_bytes(d[4:], 'little', signed=True)) for t, i, d in frames
                        if t > probe and i == 0x581 and d[:3] == bytes.fromhex('436C60')]
rebound = [{'ms_after_probe': elapsed(t, probe), 'subindex': sub, 'raw_i32': value}
           for t, sub, value in velocity_after_probe if value != 0]
assert all(item['subindex'] == 2 for item in rebound)
assert [(sub, value) for _, sub, value in velocity_after_probe[-3:]] == [(1, 0), (2, 0), (3, 0)]
final_zero = velocity_after_probe[-1][0]
preop = next(t for t in matching(0, '8001') if t > target)
preop_hb = next(t for t in matching(0x701, '7F') if t > preop)
restored = matching(0x581, '4B00200000000000')[-1]
assert final_zero < preop < preop_hb < restored
assert matching(0x581, '4B0F200001000000')[-1] > preop_hb
assert matching(0x581, '4B17100000000000')[-1] > preop_hb
assert matching(0x581, '43FF600100000000')[-1] > first_zero
assert matching(0x581, '43FF600200000000')[-1] > first_zero
raw_statuses = []
for t, i, d in frames:
    if last_target_read < t < probe and i == 0x181:
        status = d[:4].hex()
        if not raw_statuses or raw_statuses[-1]['status_bytes'] != status:
            raw_statuses.append({'ms_after_last_host_tx': elapsed(t, last_target_read), 'status_bytes': status})
pre = json.loads((root / 'target/can_preflight.json').read_text())[0]
post = json.loads((root / 'target/can_postflight.json').read_text())[0]
delta = {side: post['stats64'][side]['packets'] - pre['stats64'][side]['packets'] for side in ('rx', 'tx')}
assert delta['tx'] == sum(i in (0, 0x601) for _, i, _ in frames)
captured_rx = len(frames) - delta['tx']
rx_counter_excess = delta['rx'] - captured_rx
assert all(post['stats64'][side][key] == 0 for side in ('rx', 'tx') for key in ('errors', 'dropped'))
assert all(value == 0 for value in post['linkinfo']['info_xstats'].values())
assert post['linkinfo']['info_data']['state'] == 'ERROR-ACTIVE'
assert json.loads((root / 'jcan_config_pre.json').read_text())['data'] == json.loads((root / 'jcan_config_post.json').read_text())['data']
assert (root / 'target/executor.rc').read_text() == '0'
assert (root / 'target/capture.rc').read_text() == '0'
assert (root / 'target/capture.stderr').stat().st_size == 0
wrapper = json.loads((root / 'target/wrapper_result.json').read_text())
assert wrapper['capture_stopped'] and wrapper['executor_stopped'] and wrapper['wrapper_exit'] == 0
for side in ('rx', 'tx'):
    expected_bytes = sum(len(d) for _, i, d in frames if (i in (0, 0x601)) == (side == 'tx'))
    assert post['stats64'][side]['bytes'] - pre['stats64'][side]['bytes'] == expected_bytes
assert any(e.get('id') == 99 and e.get('ok') is True and e.get('data', {}).get('stopped') is True for e in events)
postflight = json.loads((root / 'target_process_postflight.json').read_text())
assert not postflight['residual_processes'] and postflight['can'][0]['stats64'] == post['stats64']
observation = root / 'operator_observation.json'
operator = json.loads(observation.read_text()) if observation.exists() else None
confirmed_restart = bool(operator and operator.get('right_wheel_stopped_during_silence')
                         and operator.get('right_wheel_restarted_after_stop'))
stationary = bool(operator and operator.get('both_wheels_observed_stationary'))
result = {
    'executor_result': 'PASS',
    'physical_acceptance': 'INCONCLUSIVE: no initial motion observed; RX discrepancy and timing unresolved' if stationary else 'PARTIAL: motion and subsequent stop confirmed; exact stop timing and traffic-resumption safety remain open',
    'observed_watchdog_behavior': 'both wheels observed stationary; dynamic stopping not demonstrated' if stationary else 'stop during silence followed by physical restart' if confirmed_restart else 'right wheel rotated counter-clockwise then stopped; left stationary, normal brake and sound' if operator and operator.get('right_wheel_stopped') else 'operator confirmation pending',
    'latched_stop_qualified': False,
    'recovery_limitation': 'Prior trial confirmed restart; zero sampled feedback in this run cannot exclude an unsampled brief restart.',
    'matched_frames': len(frames), 'capture_agreement': True, 'target_packet_deltas': delta,
    'captured_rx': captured_rx, 'rx_counter_excess': rx_counter_excess,
    'rx_counter_reconciliation': 'PASS' if rx_counter_excess == 0 else 'UNRESOLVED',
    'frame_ids': {f'{key:03X}': value for key, value in Counter(i for _, i, _ in frames).items()},
    'downloads': len(downloads), 'nonzero_target_requests': 1, 'watchdog_raw': 1000,
    'initial_independent_velocities': [{'subindex': sub, 'raw_i32': value} for _, sub, value in initial_velocities],
    'initial_velocity_check_ms': elapsed(initial_velocities[-1][0], initial_probe),
    'host_silence_ms': elapsed(probe, last_target_read), 'precleanup_velocities': precleanup_velocities,
    'first_cleanup_zero_ms_after_probe': elapsed(first_zero, probe),
    'shutdown_ms_after_probe': elapsed(shutdown, probe), 'velocity_rebound': rebound,
    'final_all_zero_ms_after_probe': elapsed(final_zero, probe),
    'preop_heartbeat_ms_after_probe': elapsed(preop_hb, probe), 'restoration_ms_after_probe': elapsed(restored, probe),
    'raw_status_changes_during_silence': raw_statuses,
    'timing_limitation': 'Raw status flag change is not a qualified velocity/stop timestamp; do not infer watchdog time units.',
    'restored_values': {'2000:00': 0, '1017:00': 0, '200F:00': 1, '60FF:01': 0, '60FF:02': 0},
    'can_errors_or_drops': 0, 'adapter_configuration_unchanged': True,
    'operator_observation': operator if operator else 'pending'
}
windows = {name: json.loads((root / 'target' / (name + '.json')).read_text())['can'][0]['stats64']
           for name in ('counters_capture_ready', 'counters_before_executor', 'counters_executor_exited', 'counters_before_capture_stop')}
assert windows['counters_capture_ready'] == windows['counters_before_executor'] == pre['stats64']
assert windows['counters_executor_exited'] == windows['counters_before_capture_stop'] == post['stats64']
result['counter_discrepancy_window'] = 'None in this trial; boundary snapshots stable' if rx_counter_excess == 0 else 'During executor operation; readiness/shutdown boundary changes excluded'
result['counter_snapshots'] = windows
(root / 'analysis.json').write_text(json.dumps(result, indent=2) + chr(10))
print(json.dumps(result, indent=2))
