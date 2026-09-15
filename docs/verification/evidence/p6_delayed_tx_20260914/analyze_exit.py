"""Correlate the executor-exit snapshot with already archived capture byte offsets."""
import json
import re
from pathlib import Path

out = Path(__file__).resolve().parent
trial = out.parent / 'p6_cable_retest_20260914'
exited = json.loads((trial / 'target/counters_executor_exited.json').read_text())
post = json.loads((trial / 'target/can_postflight.json').read_text())[0]
data = (trial / 'target/rk3588_can.log').read_bytes()
boundary = exited['capture_file_bytes']
before = data[:boundary].decode()
after = data[boundary:].decode()
requests = [line for line in after.splitlines() if re.search(r'can0\s+(000|601|201)\s+\[', line)]
assert len(requests) == 1 and '40 6C 60 02 00 00 00 00' in requests[0]
tx_before = exited['can'][0]['stats64']['tx']['packets']
tx_after = post['stats64']['tx']['packets']
assert tx_after - tx_before == 1
result = {'executor_exit_capture_bytes': boundary, 'capture_bytes_at_end': len(data),
    'last_captured_lines_at_exit': before.splitlines()[-4:],
    'requests_recorded_after_exit_snapshot': requests,
    'tx_packets_at_executor_exit': tx_before, 'tx_packets_at_end': tx_after,
    'evidence': 'The wrapper samples counters/file length only after child.poll() has reported exit. The extra read-only request and TX counter increment are recorded later.',
    'limit': 'No send syscall trace or controller register capture exists; queue location and enqueue time are not directly measured.'}
(out / 'exit_correlation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
