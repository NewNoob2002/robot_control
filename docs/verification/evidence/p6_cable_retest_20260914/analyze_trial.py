"""Audit the consumed trial without opening any hardware connection."""
import json
import re
from decimal import Decimal
from pathlib import Path
from trial_checks import parse_capture

out = Path(__file__).resolve().parent
raw = (out / 'target/rk3588_can.log').read_text()
frames = parse_capture(raw)
times = [Decimal(m[1]) for line in raw.splitlines() if (m := re.match(r'\s*\(([0-9.]+)\)', line))]
assert len(times) == len(frames)
errors = [(t, ident, data) for t, (ident, data) in zip(times, frames) if ident & 0x20000000]
after = [(t, ident, data) for t, (ident, data) in zip(times, frames)
         if errors and t > errors[0][0] and ident in (0, 0x601, 0x201)]
archive = [json.loads(line) for line in (out / 'jcan_session.jsonl').read_text().splitlines()]
jcan = [(f['can_id'], bytes.fromhex(f['data_hex'])) for f in archive if f.get('event') == 'frame']
normal = [(ident, data) for ident, data in frames if not ident & 0x20000000]
cursor = 0
matched = 0
for frame in normal:
    while cursor < len(jcan) and jcan[cursor] != frame:
        cursor += 1
    if cursor == len(jcan):
        break
    matched += 1
    cursor += 1
shutdown = any(f.get('op') == 'shutdown' and f.get('ok') is True
               and f.get('data', {}).get('stopped') is True for f in archive)
executor = (out / 'target/executor.log').read_text()
result = {
    'status': 'FAIL_POST_ERROR_PHYSICAL_REQUEST',
    'target_frames': len(frames), 'target_error_frames': len(errors),
    'jcan_frames': len(jcan), 'target_normal_frames': len(normal),
    'target_normal_matched_in_order': matched,
    'raw_errors': [{'time': str(t), 'id': hex(i), 'data': p.hex()} for t, i, p in errors],
    'requests_after_first_error': [{'time': str(t), 'id': hex(i), 'data': p.hex(),
        'delay_ms': str((t - errors[0][0]) * 1000)} for t, i, p in after],
    'nonzero_targets': sum(i == 0x601 and p.hex() == '23ff600300000500' for i, p in frames),
    'canopen_send_diagnostic_present': '(CO_CANsend)' in executor,
    'permission_denied_present': 'Permission denied' in executor,
    'jcan_shutdown_verified': shutdown,
    'operator_observation': 'PENDING',
    'interpretation': 'A read-only 606C:02 upload appears after the controller error. Kernel/controller queued transmission is a hypothesis, not yet established. No new nonzero target is visible; physical stop/no-restart requires operator confirmation.'}
(out / 'physical_analysis.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
