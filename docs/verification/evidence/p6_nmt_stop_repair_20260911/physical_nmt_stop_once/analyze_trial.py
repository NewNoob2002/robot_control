"""Reconcile both captures and verify fresh NMT/SDO evidence without inferring stop time."""
import json
from decimal import Decimal
from pathlib import Path

out = Path(__file__).resolve().parent
frames = []
for line in (out / 'target/rk3588_can.log').read_text().splitlines():
    fields = line.split()
    data = bytes.fromhex(''.join(fields[4:]))
    assert len(data) == int(fields[3].strip('[]'))
    frames.append((Decimal(fields[0].strip('()')), int(fields[2], 16), data))
events = [json.loads(line) for line in (out / 'jcan_session.jsonl').read_text().splitlines()]
assert all(e.get('ok') is not False and not e.get('warnings') and not e.get('error') for e in events)
observed = [e for e in events if e.get('event') == 'frame']
assert all(not any(e.get(k, False) for k in ('extended', 'fd', 'brs', 'remote')) for e in observed)
assert [(i, d) for t, i, d in frames] == [(e['can_id'], bytes.fromhex(e['data_hex'])) for e in observed]
assert any(e.get('id') == 99 and e.get('data', {}).get('stopped') for e in events)
assert [d.hex() for t, i, d in frames if i == 0] == ['0101', '0201', '8001']
start = next(t for t, i, d in frames if i == 0x601 and d.hex() == '23ff600300000500')
stop = next(t for t, i, d in frames if i == 0 and d.hex() == '0201')
stopped = next(t for t, i, d in frames if t > stop and i == 0x701 and d.hex() == '04')
preop = next(t for t, i, d in frames if t > stop and i == 0 and d.hex() == '8001')
preop_hb = next(t for t, i, d in frames if t > preop and i == 0x701 and d.hex() == '7f')
disabled = next(t for t, i, d in frames if t > stop and i == 0x601 and d.hex() == '2b40600000000000')
assert stop < stopped < preop < preop_hb < disabled
assert 1000 <= (stop - start) * 1000 < 1100
assert (stopped - stop) * 1000 < 2000
assert sum(i == 0x601 and d.hex() == '23ff600300000500' for t, i, d in frames) == 1
assert sum(i == 0x601 and d.hex() == '2b40600000000000' for t, i, d in frames if t > stop) == 1
assert not any(t > stop and i == 0x601 and d.hex() in ('2b40600006000000', '2b40600007000000', '2b4060000f000000') for t, i, d in frames)
pending = None
reads = []
transactions = 0
for t, i, d in frames:
    if i == 0x601:
        assert pending is None
        pending = (t, d)
    elif i == 0x581:
        assert pending is not None
        requested_at, request = pending
        assert request[1:4] == d[1:4]
        if request[0] == 0x40:
            assert d[0] in (0x4f, 0x4b, 0x43)
            width = {0x4f: 1, 0x4b: 2, 0x43: 4}[d[0]]
            reads.append((t, requested_at, int.from_bytes(d[1:3], 'little'), d[3], int.from_bytes(d[4:4 + width], 'little')))
        else:
            assert d[0] == 0x60 and d[4:] == bytes(4)
        transactions += 1
        pending = None
assert pending is None
latest = {(index, sub): (t, request_t, value) for t, request_t, index, sub, value in reads}
assert latest[(0x200f, 0)][2] == 1
assert latest[(0x60ff, 3)][2] == 0 and latest[(0x60ff, 3)][1] > preop
assert latest[(0x1017, 0)][2] == 0
status_t, status_req, status = latest[(0x6041, 0)]
assert status_req > disabled
assert all((status >> shift) & 0x4f == 0x40 for shift in (0, 16))
for part in (1, 2, 3):
    t, request_t, speed = latest[(0x606c, part)]
    assert request_t > disabled and speed == 0
zero_t = max(latest[(0x606c, part)][0] for part in (1, 2, 3))
assert (zero_t - disabled) * 1000 <= 2000
moving = [d for t, i, d in frames if start < t < stop and i == 0x181]
assert moving and all(len(d) == 8 and d[4:6] == bytes(2) for d in moving)
assert any(int.from_bytes(d[6:], 'little', signed=True) > 0 for d in moving)
pre = json.loads((out / 'target/can_preflight.json').read_text())[0]
post = json.loads((out / 'target/can_postflight.json').read_text())[0]
assert post['linkinfo']['info_data']['state'] == 'ERROR-ACTIVE'
for direction in ('rx', 'tx'):
    assert all(post['stats64'][direction][key] == 0 for key in ('errors', 'dropped'))
    selected = [d for t, i, d in frames if (i in (0, 0x601)) == (direction == 'tx')]
    assert post['stats64'][direction]['packets'] - pre['stats64'][direction]['packets'] == len(selected)
    assert post['stats64'][direction]['bytes'] - pre['stats64'][direction]['bytes'] == sum(map(len, selected))
assert all(v == 0 for v in post['linkinfo'].get('info_xstats', {}).values())
assert json.loads((out / 'jcan_config_pre.json').read_text())['data'] == json.loads((out / 'jcan_config_post.json').read_text())['data']
assert json.loads((out / 'coordinator_result.json').read_text())['error'] is None
wrapper = json.loads((out / 'target/wrapper_result.json').read_text())
assert wrapper['wrapper_exit'] == 0 and wrapper['capture_stopped'] and wrapper['executor_stopped']
assert (out / 'target/executor.rc').read_text() == '0'
assert (out / 'target/capture.rc').read_text() == '0'
assert (out / 'target/capture.stderr').stat().st_size == 0
result = {'protocol_and_cleanup_passed': True, 'attempts': 1, 'matched_frames': len(frames), 'sdo_transactions': transactions,
    'target_to_stop_ms': float((stop - start) * 1000), 'stop_to_stopped_heartbeat_ms': float((stopped - stop) * 1000),
    'stop_to_preop_ms': float((preop - stop) * 1000), 'stop_to_disable_voltage_ms': float((disabled - stop) * 1000),
    'stop_to_recovered_zero_sdo_ms': float((zero_t - stop) * 1000), 'final_status_raw': hex(status),
    'final_nmt': 'Pre-operational', 'final_three_speed_readbacks': [0, 0, 0], 'heartbeat_restored': 0,
    'left_tpdo_zero': True, 'right_raw_speed_range': [min(int.from_bytes(d[6:], 'little', signed=True) for d in moving), max(int.from_bytes(d[6:], 'little', signed=True) for d in moving)],
    'post_stop_tpdo_count': sum(t > stop and i == 0x181 for t, i, d in frames), 'can_errors_drops': 0,
    'limits': 'Recovered SDO zero proves cleanup, not the time NMT alone stopped the wheel. No post-Stop PDO production is required. Physical response still needs operator observation.',
    'operator': json.loads((out / 'operator_observation.json').read_text()) if (out / 'operator_observation.json').exists() else 'pending'}
(out / 'analysis.json').write_text(json.dumps(result, indent=2) + chr(10))
print(json.dumps(result, indent=2))
