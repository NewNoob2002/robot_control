"""Measure stop-command and observed zero-speed timing without conflating cleanup with stopping."""
import json
import sys
from pathlib import Path

trial = sys.argv[1]
out = Path(__file__).resolve().parent / trial
stimulus_id, stimulus_data, expected_state = {
    'shutdown': (0x601, '2B40600006000000', 0x21),
    'disable_voltage': (0x601, '2B40600000000000', 0x40),
    'quick_stop': (0x601, '2B40600002000000', 0x07),
    'nmt_stop': (0, '0201', None),
}[trial]
frames = []
for line in (out / 'target/rk3588_can.log').read_text().splitlines():
    fields = line.split()
    frames.append((float(fields[0].strip('()')), int(fields[2], 16),
                   bytes.fromhex(''.join(fields[4:]))))
start = next(t for t, i, d in frames if i == 0x601 and d == bytes.fromhex('23FF600300000500'))
stop = next(t for t, i, d in frames if t > start and i == stimulus_id and d == bytes.fromhex(stimulus_data))
assert 1000 <= (stop - start) * 1000 < 1100
feedback = [(t, d) for t, i, d in frames if t > stop and i == 0x181]
assert feedback and all(len(d) == 8 for t, d in feedback)
zeros = [t for t, d in feedback if d[4:] == bytes(4)]
assert zeros and feedback[-1][1][4:] == bytes(4)
nonzero = [t for t, d in feedback if d[4:] != bytes(4)]
last_nonzero = max(nonzero, default=stop)
trailing_zero = next(t for t in zeros if t > last_nonzero)
if expected_state is not None:
    mask = 0x4F if trial == 'disable_voltage' else 0x6F
    assert any(all(int.from_bytes(d[k:k + 2], 'little') & mask == expected_state
                   for k in (0, 2)) for t, d in feedback)
else:
    assert any(t > stop and i == 0x701 and d == bytes([4]) for t, i, d in frames)
result = {
    'trial': trial,
    'target_to_stop_command_ms': round((stop - start) * 1000, 3),
    'stop_to_first_zero_tpdo_ms': round((zeros[0] - stop) * 1000, 3),
    'stop_to_trailing_zero_tpdo_ms': round((trailing_zero - stop) * 1000, 3),
    'right_speed_raw_after_stop_range': [min(int.from_bytes(d[6:8], 'little', signed=True) for t, d in feedback),
                                         max(int.from_bytes(d[6:8], 'little', signed=True) for t, d in feedback)],
    'nonzero_feedback_after_first_zero': any(t > zeros[0] for t in nonzero),
    'expected_protocol_state_observed': True,
    'limits': 'Wire timestamps and finite sampled feedback only; trailing zero does not prove continuous zero between samples. NMT Stop suppresses TPDO, so recovered feedback cannot establish the physical stop instant.',
}
(out / 'stop_timing.json').write_text(json.dumps(result, indent=2) + chr(10))
print(json.dumps(result, indent=2))
