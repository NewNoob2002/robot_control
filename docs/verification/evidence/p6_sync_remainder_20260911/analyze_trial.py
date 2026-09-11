"""Validate one bounded synchronous stop/loss trial using both raw captures."""
import json
import sys
from pathlib import Path

trial = sys.argv[1]
out = Path(__file__).resolve().parent / trial
frames = []
for number, line in enumerate((out / 'target/rk3588_can.log').read_text().splitlines(), 1):
    fields = line.split()
    data = bytes.fromhex(''.join(fields[4:]))
    assert len(data) == int(fields[3].strip('[]'))
    frames.append((float(fields[0].strip('()')), int(fields[2], 16), data, number))
observed = []
for line in (out / 'jcan_session.jsonl').read_text().splitlines():
    event = json.loads(line)
    if event.get('event') == 'frame':
        observed.append((event['can_id'], bytes.fromhex(event['data_hex'])))
assert [(i, d) for t, i, d, n in frames] == observed
commands = [f for f in frames if f[1] == 0x601 and f[2] == bytes.fromhex('23FF600300000500')]
assert len(commands) == 1
start = commands[0][0]
zero = next(t for t, i, d, n in frames if t > start and i == 0x601 and d == bytes.fromhex('23FF600300000000'))
pending = None
reads = {}
transactions = 0
for t, i, d, n in frames:
    if i == 0x601:
        assert pending is None
        pending = d
    elif i == 0x581:
        assert pending is not None and pending[1:4] == d[1:4] and d[0] != 0x80
        transactions += 1
        if pending[0] == 0x40:
            reads[(int.from_bytes(d[1:3], 'little'), d[3])] = int.from_bytes(d[4:], 'little')
        else:
            assert d[0] == 0x60
        pending = None
assert pending is None
assert reads[(0x200F, 0)] == 1
assert all(reads[k] == 0 for k in [(0x60FF, 3), (0x606C, 1), (0x606C, 2), (0x606C, 3), (0x1017, 0)])
tpdos = [f for f in frames if f[1] == 0x181]
assert all(len(f[2]) == 8 and f[2][4:6] == bytes(2) for f in tpdos)
moving = [f for f in tpdos if start < f[0] < zero and int.from_bytes(f[2][6:8], 'little', signed=True) > 0]
assert moving
assert tpdos[-1][2][4:] == bytes(4)
assert [d for t, i, d, n in frames if i == 0x701][-1] == bytes([127])
pre = json.loads((out / 'target/can_preflight.json').read_text())[0]
post = json.loads((out / 'target/can_postflight.json').read_text())[0]
assert post['linkinfo']['info_data']['state'] == 'ERROR-ACTIVE'
for direction in ('rx', 'tx'):
    assert all(post['stats64'][direction][key] == 0 for key in ('errors', 'dropped'))
    selected = [f for f in frames if (f[1] in (0, 0x601)) == (direction == 'tx')]
    assert post['stats64'][direction]['packets'] - pre['stats64'][direction]['packets'] == len(selected)
    assert post['stats64'][direction]['bytes'] - pre['stats64'][direction]['bytes'] == sum(len(f[2]) for f in selected)
assert json.loads((out / 'coordinator_result.json').read_text())['error'] is None
assert json.loads((out / 'target/wrapper_result.json').read_text())['wrapper_exit'] == 0
result = {'trial': trial, 'protocol_and_cleanup_passed': True, 'attempts': 1,
    'frames': len(frames), 'capture_pairs_equal': True, 'sdo_transactions': transactions,
    'target_to_zero_ms': round((zero - start) * 1000, 3),
    'right_tpdo_raw_range': [min(int.from_bytes(f[2][6:], 'little', signed=True) for f in moving), max(int.from_bytes(f[2][6:], 'little', signed=True) for f in moving)],
    'left_tpdo_zero': True, 'zero_feedback_verified': True, 'can_errors_drops': 0,
    'final_status_raw': tpdos[-1][2][:4].hex(), 'mode_kept': 1, 'physical_observation': 'pending'}
if trial in ('watchdog', 'heartbeat_loss', 'tpdo_loss'):
    assert reads[(0x2000, 0)] == 0
    assert reads[(0x1800, 5)] == 100
if trial == 'watchdog':
    previous_tx = max(t for t, i, d, n in frames if start < t < zero and i in (0, 0x601))
    assert (zero - previous_tx) * 1000 >= 1500
    stopped = next(f for f in tpdos if f[0] > moving[-1][0] and f[2][4:] == bytes(4))
    assert stopped[0] < zero
    result['tx_quiet_ms'] = round((zero - previous_tx) * 1000, 3)
    result['last_request_to_first_zero_tpdo_ms'] = round((stopped[0] - previous_tx) * 1000, 3)
    result['last_nonzero_tpdo_to_zero_tpdo_ms'] = round((stopped[0] - moving[-1][0]) * 1000, 3)
    result['resume_first_tx_is_packed_zero'] = True
if (out / 'operator_observation.json').exists():
    result['physical_observation'] = json.loads((out / 'operator_observation.json').read_text())
(out / 'analysis.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
