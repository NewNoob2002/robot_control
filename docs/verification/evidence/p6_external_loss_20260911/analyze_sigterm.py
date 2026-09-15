"""Validate the one-shot SIGTERM capture and report wire timing."""
import json
from decimal import Decimal
from pathlib import Path

root = Path(__file__).resolve().parent / 'moving_sigterm_once'
frames = []
for line in (root / 'target/rk3588_can.log').read_text().splitlines():
    fields = line.split()
    frames.append((Decimal(fields[0].strip('()')), int(fields[2], 16), bytes.fromhex(''.join(fields[4:]))))
jcan = [json.loads(line) for line in (root / 'jcan_session.jsonl').read_text().splitlines()]
wire = [(f['can_id'], bytes.fromhex(f['data_hex'])) for f in jcan if f.get('event') == 'frame']
assert wire == [(i, d) for _, i, d in frames]
assert len(frames) == 135
signal = json.loads((root / 'target/sigterm_event.json').read_text())
at = Decimal(signal['wall_before_ns']) / 1000000000
assert signal['moving_tpdo_observed']
assert (root / 'target/executor.rc').read_text().strip() == '1'
assert 'qualification_owner_exit: interface=can0 remote=1 cleanup=verified: Operation canceled' in (root / 'target/executor.log').read_text()
nonzero = [(t, d) for t, i, d in frames if i == 0x601 and d == bytes.fromhex('23FF600300000500')]
assert len(nonzero) == 1 and nonzero[0][0] < at
later = [(t, i, d) for t, i, d in frames if t > at]
zero = next(t for t, i, d in later if i == 0x601 and d == bytes.fromhex('23FF600300000000'))
shutdown = next(t for t, i, d in later if i == 0x601 and d == bytes.fromhex('2B40600006000000'))
assert not any(i == 0 and d == bytes([1, 1]) for _, i, d in later)
assert not any(i == 0x601 and d[:4] == bytes.fromhex('2B406000') and d[4] in (7, 15) for _, i, d in later)
tpdo = [(t, d) for t, i, d in later if i == 0x181]
assert tpdo and all(d[4:6] == bytes(2) for _, d in tpdo)
assert tpdo[-1][1][4:] == bytes(4)
assert tpdo[-1][1][:4] == bytes.fromhex('21142114')
final = []
for sub in (1, 2, 3):
    final.append(max(t for t, i, d in later if i == 0x581 and d[:4] == bytes([0x43, 0x6C, 0x60, sub]) and d[4:] == bytes(4)))
assert max(final) < next(t for t, i, d in later if i == 0 and d == bytes([0x80, 1]))
assert json.loads((root / 'operator_observation.json').read_text())['accepted']
result = {'passed': True, 'matching_frames': len(frames), 'sigterm_to_zero_request_ms': float((zero-at)*1000), 'sigterm_to_shutdown_ms': float((shutdown-at)*1000), 'sigterm_to_final_zero_sdo_ms': float((max(final)-at)*1000), 'cleanup_verified': True, 'operator_accepted': True, 'limits': 'Cross-clock wire timing is approximate; sampled speed includes signed deceleration rebound and does not establish continuous zero.'}
(root / 'test_result.json').write_text(json.dumps(result, indent=2) + chr(10))
print(json.dumps(result, indent=2))
