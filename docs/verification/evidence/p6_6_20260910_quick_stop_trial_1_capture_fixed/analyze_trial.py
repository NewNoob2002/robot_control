#!/usr/bin/env python3
"""Reconcile the actual Quick Stop captures and assert its acceptance checks."""
from pathlib import Path
from decimal import Decimal
from datetime import datetime, timezone
import json
P = Path(__file__).resolve().parent
records = []
for line in (P / 'target/rk3588_can.log').read_text().splitlines():
    fields = line.split()
    timestamp = int(Decimal(fields[0].strip('()')) * 1000000)
    ident = int(fields[2], 16)
    dlc = int(fields[3].strip('[]'))
    data = bytes.fromhex(' '.join(fields[4:]))
    assert len(data) == dlc
    records.append((timestamp, ident, data))
events = [json.loads(line) for line in (P / 'jcan_session.jsonl').read_text().splitlines()]
frames = [event for event in events if event.get('event') == 'frame']
assert [(ident, data) for _, ident, data in records] == [(f['can_id'], bytes.fromhex(f['data_hex'])) for f in frames]
assert not any(e.get('ok') is False or e.get('warnings') or e.get('error') for e in events)
assert any(e.get('op') == 'shutdown' and e.get('ok') and e['data']['stopped'] for e in events)
assert all(not any(f.get(k, False) for k in ('extended', 'fd', 'brs', 'remote')) for f in frames)
assert int((P / 'target/executor.rc').read_text()) == 0
assert (P / 'target/capture.stderr').read_text() == ''
assert (P / 'jcan_err.log').read_text() == ''
assert (P / 'remote_err.log').read_text() == ''
assert (P / 'transfer.stderr').read_text() == ''
coordinator = json.loads((P / 'coordinator_result.json').read_text())
assert coordinator['error'] is None and coordinator['executor_started']
assert coordinator['downloads'] <= 16 and coordinator['uploads'] <= 300 and coordinator['nmt_frames'] <= 2
outstanding = None
uploads = []
acks = []
requests = []
for t, ident, data in records:
    if ident == 0x601:
        assert outstanding is None
        outstanding = (t, data)
        requests.append((t, data))
    if ident == 0x581:
        assert outstanding is not None
        request_t, request = outstanding
        assert data[1:4] == request[1:4] and data[0] != 0x80
        index, sub = int.from_bytes(data[1:3], 'little'), data[3]
        if request[0] == 0x40:
            assert data[0] in (0x43, 0x47, 0x4B, 0x4F)
            size = 4 - ((data[0] >> 2) & 3)
            value = int.from_bytes(data[4:4 + size], 'little', signed=True)
            uploads.append({'t': t, 'index': index, 'subindex': sub, 'size': size, 'value': value})
        else:
            assert data[0] == 0x60
            acks.append((request_t, t, request))
        outstanding = None
assert outstanding is None
nonzero = [(t, d) for t, d in requests if d[0] == 0x23 and d[1:3] == bytes([0xFF, 0x60]) and any(d[4:])]
assert len(nonzero) == 1 and nonzero[0][1] == bytes.fromhex('23 FF 60 02 05 00 00 00')
stop = [(t, d) for t, d in requests if d == bytes.fromhex('2B 40 60 00 02 00 00 00')]
assert len(stop) == 1
stop_t = stop[0][0]
assert 2000000 <= stop_t - nonzero[0][0] <= 2020000
assert not any(t > stop_t and d[1:3] == bytes([0x40, 0x60]) and d[0] != 0x40 for t, d in requests)
ack_t = next(t for request_t, t, _ in acks if request_t == stop_t)
state_t, state_data = next((t, d) for t, ident, d in records if t > stop_t and ident == 0x181 and len(d) == 8 and int.from_bytes(d[:2], 'little') & 0x6F == 7 and int.from_bytes(d[2:4], 'little') & 0x6F == 7)
velocity = [u for u in uploads if u['index'] == 0x606C and u['t'] > stop_t]
first_zero = next(u['t'] for u in velocity if u['subindex'] == 2 and u['value'] == 0)
first_zero_times = {s: next(u['t'] for u in velocity if u['subindex'] == s and u['value'] == 0) for s in (1, 2, 3)}
all_zero_t = max(first_zero_times.values())
assert all_zero_t - stop_t <= 2000000
preop_t = next(t for t, ident, d in records if t > stop_t and ident == 0 and d == bytes([0x80, 1]))
preop_hb_t = next(t for t, ident, d in records if t > preop_t and ident == 0x701 and d == bytes([0x7F]))
latest = {(u['index'], u['subindex']): u for u in uploads}
for key, value in { (0x1017, 0): 0, (0x200F, 0): 1, (0x60FF, 1): 0, (0x60FF, 2): 0, (0x606C, 1): 0, (0x606C, 2): 0, (0x606C, 3): 0 }.items():
    assert latest[key]['value'] == value and latest[key]['t'] > stop_t
assert latest[(0x605A, 0)]['value'] == 5 and latest[(0x605A, 0)]['size'] == 2
assert not any(d[1:3] == bytes([0x5A, 0x60]) and d[0] != 0x40 for _, d in requests)
pre = json.loads((P / 'target/can_preflight.json').read_text())[0]
post = json.loads((P / 'target/can_postflight.json').read_text())[0]
assert post['linkinfo']['info_data']['state'] == 'ERROR-ACTIVE'
assert pre['linkinfo']['info_xstats'] == post['linkinfo']['info_xstats']
for direction in ('rx', 'tx'):
    for field in ('errors', 'dropped'):
        assert post['stats64'][direction][field] == pre['stats64'][direction][field] == 0
rx_delta = post['stats64']['rx']['packets'] - pre['stats64']['rx']['packets']
tx_delta = post['stats64']['tx']['packets'] - pre['stats64']['tx']['packets']
assert tx_delta == coordinator['downloads'] + coordinator['uploads'] + coordinator['nmt_frames']
assert rx_delta + tx_delta == len(records)
assert json.loads((P / 'jcan_config_pre.json').read_text()) == json.loads((P / 'jcan_config_post.json').read_text())
operator = json.loads((P / 'operator_observation.json').read_text()) if (P / 'operator_observation.json').exists() else {}
result = {'electronic_hil_passed': True, 'operator_observation_pending': True, 'timestamp_utc': datetime.fromtimestamp(records[0][0] / 1000000, timezone.utc).isoformat(), 'frames': len(records), 'sequences_equal': True, 'target_rx_delta': rx_delta, 'target_tx_delta': tx_delta, 'requests': {'sdo_downloads': coordinator['downloads'], 'sdo_uploads': coordinator['uploads'], 'nmt': coordinator['nmt_frames'], 'nonzero': 1, 'quick_stop': 1}, 'timings_ms': {'nonzero_interval': (stop_t - nonzero[0][0]) / 1000, 'quick_stop_ack': (ack_t - stop_t) / 1000, 'dual_quick_stop_active': (state_t - stop_t) / 1000, 'right_velocity_first_zero': (first_zero - stop_t) / 1000, 'all_velocity_views_zero': (all_zero_t - stop_t) / 1000, 'nmt_preoperational': (preop_t - stop_t) / 1000, 'preoperational_heartbeat': (preop_hb_t - stop_t) / 1000, 'restoration_complete': (max(latest[(0x1017, 0)]['t'], latest[(0x200F, 0)]['t']) - stop_t) / 1000}, 'raw_dual_status': [hex(int.from_bytes(state_data[:2], 'little')), hex(int.from_bytes(state_data[2:4], 'little'))], 'velocity_readbacks_after_stop': [{**u, 'elapsed_ms': (u['t'] - stop_t) / 1000} for u in velocity], 'cleanup': {'targets_zero': True, 'velocities_zero': True, 'heartbeat_restored_to': 0, 'command_application_restored_to': 1, 'no_terminal_retry_or_reenable': True, 'jcan_configuration_unchanged': True}}
result['operator_observation_pending'] = not operator.get('confirmed', False)
result['passed'] = result['electronic_hil_passed'] and not result['operator_observation_pending']
result['timings_ms']['first_all_velocity_views_zero'] = result['timings_ms'].pop('all_velocity_views_zero')
result['timings_ms']['cleanup_all_velocity_views_zero'] = (max(latest[(0x606C, s)]['t'] for s in (1, 2, 3)) - stop_t) / 1000
result['velocity_feedback_rebounded_after_first_zero'] = any(u['t'] > all_zero_t and u['value'] != 0 for u in velocity)
(P / 'analysis.json').write_text(json.dumps(result, indent=2) + chr(10))
print(json.dumps({k: v for k, v in result.items() if k != 'velocity_readbacks_after_stop'}, indent=2))
